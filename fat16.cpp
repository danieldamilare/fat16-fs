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

template<typename T>
constexpr auto  to_le_array(T num){
    std::array<T, sizeof(T)> arr{};
    int tsize = sizeof(T);
    for (int i = 0; i < sizeof(T); i++){
        if(is_little_endian()){
            arr[i] = static_cast<uint8_t>((num >>(i * 8)) & 0xff);
        }
        else{
            arr[tsize -i - 1] = static_cast<uint8_t>((num >>(i * 8)) & 0xff);
        }
    }
    return arr;
}

template <size_t N>
constexpr uint64_t arr_to_num(const uint8_t (&arr)[N]) {
    size_t n = (N > 8) ? 8 : N; // only up to 64 bits
    uint64_t num = 0;

    for (size_t i = 0; i < n; ++i)
        num |= static_cast<uint64_t>(arr[i]) << (8 * i);

    return num;
}

DiskImg::DiskImg(int size){
    this->size = ((size + (sector_size -1))/sector_size) * sector_size;
    base = new std::byte[this->size]();
    img = base + 512; //leave the first 512 for partitioning
}

DiskImg::~DiskImg(){
    delete [] base;
}

SectorInfo DiskImg::read_sector(size_t sector, size_t num, size_t part_start){
    if (num == 0 ) return {};
    size_t to_start = part_start + sector * sector_size;
    size_t to_read = num * sector_size;

    if (to_start + to_read > size) return {};
    SectorInfo sect;
    sect.resize(to_read);

    std::copy_n(img+to_start, to_read, sect.data());
    return sect;
}

DiskStatus DiskImg::write_sector(size_t sector, SectorInfo& sect, size_t part_start){
    if (sect.empty()) return DiskStatus::BAD_SIZE;
    size_t to_start = part_start + (sector * byte_per_sector);
    size_t to_write = sect.size();
    if (to_start + to_write > size) return DiskStatus::OUT_OF_RANGE;
    std::copy_n(sect.data(), to_write, img+to_start);
    return DiskStatus::OK;
}

DiskStatus DiskImg::create_partition(size_t size, size_t& part_size){
    //write to mbr partition table
    return DiskStatus::UNKNOWN_ERROR;
}

Fat16Fs::Fat16Fs(DiskImg& disk): Disk{disk}{}

uint8_t get_sector_per_cluster(size_t bytes){
    std::vector<std::pair<size_t, size_t>> size_range {
        {256 * 1024L * 1024, 8},
        {512 * 1024L * 1024, 16},
        {1024 * 1024L * 1024, 32},
        {2 * 1024 * 1024L * 1024, 64}
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
    constexpr auto num_root_entries = 512;
    constexpr auto root_entrysz = 32;
    constexpr auto reserved_sector = 1;
    uint8_t jmp[3] = {0xeb, 0x3f, 0x90};
    uint8_t oem[8] = "MYDOSNG";
    oem[7] = ' ';

    uint8_t bps[2] = {0x00, 0x02}; // 512 (0x200) bytes per sector
    uint8_t spc = get_sector_per_cluster(bytes);
    // set the total_sector
   
    std::vector<std::byte> boot_file(512, std::byte{0x00});

    total_sector = bytes/byte_per_sector;
    FatBootSector * BootSect =reinterpret_cast<FatBootSector *>(boot_file.data() );
    uint8_t num_fat = 2;
    uint8_t less_sector[2] = {0, 0};
    uint8_t higher_sector[4] = {0, 0, 0, 0};

    if (total_sector < (1 << 16)){
        less_sector[0] = (total_sector & 0xff);
        less_sector[1] = ((total_sector >> 8) & 0xff);
    } else{
        higher_sector[0] = (total_sector & 0xff);
        higher_sector[1] = ((total_sector >> 8) & 0xff);
        higher_sector[2] = ((total_sector >> 16) & 0xff);
        higher_sector[3] = ((total_sector >> 24) & 0xff);
    }

    uint8_t media_descriptor = 0xfa; // ramdisk
    auto cluster_est = (total_sector - (num_root_entries * root_entrysz + byte_per_sector * reserved_sector))/spc; // try to estimate cluster

    uint16_t fat_sector = ((cluster_est * 2) + (byte_per_sector -1))/byte_per_sector;
    uint8_t num_sector_per_fat[2] = {static_cast<uint8_t>(fat_sector & 0xff), 
                                     static_cast<uint8_t>((fat_sector >> 8)&0xff )};
    uint8_t num_sect_per_track[2] = {0, 0};
    uint8_t num_head[2] = {0, 0};
    uint8_t num_hidden_sector[4] = {0, 0, 0, 0};
    uint8_t label[11] = {'N', 'O', ' ', 'N', 'A', 'M', 'E', ' ', ' ', ' ', ' '};
    uint8_t sys_ident[8] = {'F', 'A', 'T', '1', '6', ' ', ' ', ' '};
    uint8_t drive_num = 0x80;
    uint8_t signature[2] = {0x55, 0xaa};

    uint8_t rsv_sector[2] = {0x01, 0x00};
    std::copy_n(jmp, sizeof(jmp), BootSect->jmp);
    std::copy_n(oem, sizeof(oem), BootSect->oem);
    std::copy_n(bps, sizeof(bps), BootSect->bps);
    std::copy_n(less_sector, sizeof(less_sector), BootSect->num_sect);
    std::copy_n(higher_sector, sizeof(higher_sector), BootSect->sector_count);
    std::copy_n(label, sizeof(label), BootSect->label);
    std::copy_n(num_head, sizeof(num_head), BootSect->num_head);
    std::copy_n(num_sector_per_fat, sizeof(num_sector_per_fat), BootSect->num_sect_fat);
    std::copy_n(sys_ident, sizeof(sys_ident), BootSect->sys_ident);
    std::copy_n(num_sect_per_track, sizeof(num_sect_per_track),
            BootSect->num_sect_per_track);
    std::copy_n(num_hidden_sector, sizeof(num_hidden_sector), 
            BootSect->num_hidden_sector);
    std::copy_n(signature, sizeof(signature), BootSect->signature);
    std::copy_n(rsv_sector, 2, BootSect->rsv_sector);
    std::copy_n(bps, sizeof(bps), BootSect->num_rootdir); // root dir is 512

    BootSect->spc = spc;
    BootSect->num_fat = num_fat;
    BootSect->desc_type =media_descriptor;
    BootSect->drive_num = drive_num;
    BootSect->boot_sig = 0x29;

    std::copy_n(BootSect, 512, &bpb);

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
    if (bytes > (2LL * 1024LL * 1024LL * 1024LL)){
        std::cerr << "Error: Fat16 support at most 2gb";
        return DiskStatus::PARTITION_FAIL;
    }

    bytes = byte_per_sector * (bytes +(byte_per_sector -1))/byte_per_sector;

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

int main(){

}
