#include <cassert>
#include <iostream>
#include "fat16.h"
#include <algorithm>
#include <cstddef>
#include <vector>

static constexpr size_t  byte_per_sector = 512; // our simulated hardisk has a 512 sector

inline bool is_little_endian() noexcept {
    uint16_t x = 1;
    return *reinterpret_cast<uint8_t*>(&x) == 1;
}

template<size_t N>
constexpr uint64_t arr_to_num(const uint8_t (&arr)[N]) {
    size_t n = (N > 8) ? 8 : N; // only up to 64 bits
    uint64_t num = 0;

    for (size_t i = 0; i < n; ++i)
        num |= static_cast<uint64_t>(arr[i]) << (8 * i);

    return num;
}

template<typename T>
constexpr void write_le(uint8_t * dest, T num){
    auto tsize = sizeof(T);
    for (size_t i = 0; i < tsize; i++){
            dest[i] = static_cast<uint8_t>((num >>(i * 8)) & 0xff);
        }
}

DiskImg::DiskImg(size_t size){
    this->size = ((size + (sector_size -1))/sector_size) * sector_size;
    printf("In disk constructor size: %lu", size);
    base = new std::byte[this->size];
    img = base + 512; //leave the first 512 for partitioning
}

DiskImg::~DiskImg(){
    delete [] base;
}

SectorInfo DiskImg::read_sector(size_t sector, size_t num, size_t part_start){
    if (num == 0 ) return {};
    size_t to_start = part_start + sector * sector_size;
    size_t to_read = num * sector_size;

    if (to_start + to_read > (size - 512)) return {};
    SectorInfo sect;
    sect.resize(to_read);

    std::copy_n(img+to_start, to_read, sect.data());
    return sect;
}


DiskStatus DiskImg::save_sectors(std::string filename, size_t sector,
        size_t sector_count, size_t part_start){
    size_t write_size = sector_count * sector_size;
    size_t to_start = sector * sector_size;

    FILE * f = fopen(filename.c_str(), "wb");
    if  (f == NULL){
        std::cerr << "Can open file to save sectors\n";
        return DiskStatus::UNKNOWN_ERROR;
    }

    if(auto size = fwrite(img+to_start, 1, write_size, f); size != write_size){
        std::cerr << "Error writing sector " << sector << " with size: " << write_size << " to file: " << filename << "\n";
        return DiskStatus::UNKNOWN_ERROR;
    }
    fclose(f);
    return DiskStatus::OK;
}


DiskStatus DiskImg::write_sector(size_t sector, SectorInfo& sect, size_t part_start){
    if (sect.empty()) return DiskStatus::BAD_SIZE;
    size_t to_start = part_start + (sector * byte_per_sector);
    size_t to_write = sect.size();
    if (to_start + to_write > (size - 512)) return DiskStatus::OUT_OF_RANGE;
    std::copy_n(sect.data(), to_write, img+to_start);
    return DiskStatus::OK;
}

DiskStatus DiskImg::create_partition(size_t size, size_t& part_start){
    //write to mbr partition table
    // place holder currently to test the function
    part_start = begin_partition_sector * sector_size;
    return DiskStatus::OK;
}

Fat16Fs::Fat16Fs(DiskImg& disk): Disk{disk}{}

uint8_t get_sector_per_cluster(size_t bytes){
    std::vector<std::pair<size_t, size_t>> size_range {
        {32 * 1024LL * 1024LL, 1},
        {64 * 1024LL * 1024LL, 2},
        {128LL * 1024LL * 1024LL, 4},
        {256LL * 1024LL * 1024LL, 8},
        {512LL * 1024LL * 1024LL, 16},
        {1024LL * 1024LL * 1024LL, 32},
        {2LL * 1024LL * 1024LL * 1024LL, 64}
    };
    auto spc = 4;
    for (auto [file_size, sector_size]: size_range){
        if (bytes < file_size){
            spc = sector_size;
            break;
        }
    }
    return spc;
}

DiskStatus Fat16Fs::write_fat16_boot(size_t bytes){
    constexpr uint16_t num_root_entries = 512;
    constexpr uint8_t root_entrysz = 32;
    constexpr uint16_t reserved_sector = 1;
    constexpr uint16_t bps = 512;

    uint8_t spc = get_sector_per_cluster(bytes);
    // set the total_sector
   
    printf("Before allocating boot file\n");
    std::vector<std::byte> boot_file(512, std::byte{0x00});
    printf("After allocating boot file\n");

    total_sector = bytes/byte_per_sector;
    FatBootSector * BootSect =reinterpret_cast<FatBootSector *>(boot_file.data() );
    printf("Reinterpeted Boot sector\n");
    uint8_t num_fat = 2;
    
    uint8_t media_descriptor = 0xfa; // ramdisk
    auto root_sec = (num_root_entries * root_entrysz)/ byte_per_sector;
    auto cluster_est = (total_sector - root_sec - reserved_sector)/spc; // try to estimate cluster
    uint16_t fat_sector = ((cluster_est * 2) + (byte_per_sector -1))/byte_per_sector;
    auto actual_cluster = (total_sector - root_sec - (fat_sector * 2) -reserved_sector)/spc;
    if (actual_cluster >= (1 <<16)){//overflow!!!
            std::cerr << cluster_est << "\n";
            std::cerr << "Cluster too large for file system";
            return DiskStatus::OUT_OF_RANGE;
    }
    if (actual_cluster < 4085){
        std::cerr << "Too few cluster would be FAT12: " << cluster_est << "\n";
    }

    fat_sector = ((actual_cluster * 2) + (byte_per_sector -1))/byte_per_sector;
    write_le(BootSect->num_sect_fat, static_cast<uint16_t>(fat_sector));

    uint8_t jmp[3] = {0xeb, 0x3f, 0x90};
    std::copy_n(jmp, sizeof(jmp), BootSect->jmp);
    write_le(BootSect->num_hidden_sector, static_cast<uint32_t>(0));
    uint8_t label[11] = {'N', 'O', ' ', 'N', 'A', 'M', 'E', ' ', ' ', ' ', ' '};
    std::copy_n(label, sizeof(label), BootSect->label);
    uint8_t sys_ident[8] = {'F', 'A', 'T', '1', '6', ' ', ' ', ' '};
    std::copy_n(sys_ident, sizeof(sys_ident), BootSect->sys_ident);
    uint8_t drive_num = 0x80;
    BootSect->signature[0] = 0x55; BootSect->signature[1] = 0xaa;
    write_le(BootSect->num_sect_per_track, static_cast<uint16_t>(63));
    write_le(BootSect->num_head, static_cast<uint16_t>(255));
    write_le(BootSect->rsv_sector, static_cast<uint16_t>(reserved_sector));
    write_le(BootSect->bps, static_cast<uint16_t>(bps));

    if(total_sector < (1 << 16)){
        write_le(BootSect->num_sect, static_cast<uint16_t> (total_sector));
        write_le(BootSect->sector_count, static_cast<uint32_t>(0));
    } else{
        write_le(BootSect->num_sect, static_cast<uint16_t> (0));
        write_le(BootSect->sector_count, static_cast<uint32_t>(total_sector));
    }
    write_le(BootSect->num_rootdir, static_cast<uint16_t>(num_root_entries));
    uint8_t oem[8] = "MYDOSNG";
    oem[7] = ' ';
    std::copy_n(oem, sizeof(oem), BootSect->oem);
    printf("All copying done");

    BootSect->spc = spc;
    BootSect->num_fat = num_fat;
    BootSect->desc_type =media_descriptor;
    BootSect->drive_num = drive_num;
    BootSect->boot_sig = 0x29;

    bpb = *BootSect;
    if(auto status =Disk.write_sector(0, boot_file, part_start); status != DiskStatus::OK){
        std::cerr << "Error formating the Fat drive";
        return status;
    }
    fat_table_sector_start = arr_to_num(bpb.rsv_sector);
    fat_sectors = arr_to_num(bpb.num_sect_fat);
    root_dir_sector_start = fat_table_sector_start + fat_sectors * bpb.num_fat;
    root_dir_sectors = (arr_to_num(bpb.num_rootdir) * 32 + byte_per_sector-1)/byte_per_sector;
    data_area_start = root_dir_sector_start + root_dir_sectors;
    data_area_sectors = total_sector - data_area_start;
    sector_per_cluster = bpb.spc;
    total_cluster = data_area_sectors/sector_per_cluster;

    return DiskStatus::OK;
}

DiskStatus Fat16Fs::format_fat_table(){
    auto fat_table = Disk.read_sector(fat_table_sector_start, fat_sectors, part_start);
    if (fat_table.empty()){
        std::cerr << "fat table read empty\n";
        return DiskStatus::READ_FAIL;
    }

    std::fill(fat_table.begin(), fat_table.end(), std::byte{0});
    // first cluster is reserved
    fat_table[0] = std::byte{0xfa};
    fat_table[1] = std::byte{0xff};
    fat_table[2] = std::byte{0xff};
    fat_table[3] = std::byte{0xff};
    if (auto status = Disk.write_sector(fat_table_sector_start,
                fat_table, part_start); status != DiskStatus::OK) 
        return status;

    return Disk.write_sector(fat_table_sector_start + fat_sectors,
                fat_table, part_start);

}

DiskStatus Fat16Fs::format_dir_entries(){
    std::vector<std::byte> root_dir{root_dir_sectors * byte_per_sector, std::byte{0}};
    return Disk.write_sector(root_dir_sector_start, root_dir, part_start);
}

DiskStatus Fat16Fs::fat_format(long long bytes){
    if (bytes > (2LL * 1024LL * 1024LL * 1024LL) || 
            bytes <  2LL * 1024LL * 1024LL){
        std::cerr << "Bytes: "<< bytes << "\n";
        std::cerr << "Attempting to create a too small or a too large filesystem";
        return DiskStatus::BAD_SIZE;
    }

    bytes = byte_per_sector * ((bytes +(byte_per_sector -1))/byte_per_sector);

    if(auto status = Disk.create_partition(bytes, part_start);
            status != DiskStatus::OK){
            std::cerr << "Error formating disk..\n";
            return status;
    }

    if(auto status = write_fat16_boot(bytes); status != DiskStatus::OK){
        std::cerr << "Error writing boot sector\n";
        return status;
    }
     if(auto status = format_fat_table(); status != DiskStatus::OK){
        std::cerr << "Error Writing Fat Table\n";
        return status;
    }

   if (auto status = format_dir_entries();status != DiskStatus::OK){
        std::cerr << "Error formating directories entry";
        return status;
    }
    return DiskStatus::OK;
}

Fat16Fs::~Fat16Fs(){
}


DiskStatus Fat16Fs::fat_write(std::string filename){
   return  Disk.save_sectors(filename,
            0, total_sector, part_start);
}

int main(){
    printf("In main\n");
    DiskImg tempdisk{4LL * 1024LL * 1024LL};
    Fat16Fs myfat (tempdisk);
    printf("Formatting...\n");
    myfat.fat_format(3LL * 1024LL * 1024LL);
    myfat.fat_write(std::string{"testfat.bin"});
}
