#ifndef FAT16_H
#define FAT16_H
/* An in-memory fat16 filesystem
 * Oct 13 2025
 */
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

using SectorInfo = std::vector<std::byte>;
enum class DiskStatus {
    OK,
    OUT_OF_RANGE,
    BAD_SIZE,
    READ_FAIL,
    PARTITION_FAIL,
    UNKNOWN_ERROR
};

struct PartInfo{
    std::string part_name;
    int part_start;
    int part_end;
};

class DiskImg{
    public:
        DiskImg(size_t size);
        DiskImg(const DiskImg& other) = delete;
        DiskImg& operator=(const DiskImg&) = delete;

        SectorInfo read_sector(size_t sector, size_t num=1, size_t part_start = 0);
        DiskStatus write_sector(size_t sector, SectorInfo& sect,  size_t part_start = 0);
        DiskStatus save_partition(size_t part_start);
        DiskStatus save_disk();
        DiskStatus save_sectors(std::string filename, size_t sector, size_t sector_count=1, size_t part_start = 0);

        DiskStatus create_partition(size_t size, size_t& part_start);

        std::vector<PartInfo> list_partitions();
    ~DiskImg();

    private:
        std::byte * img;
        std::byte * base;
        size_t size;
        size_t sector_size = 512;
        size_t begin_partition_sector = 0;
}; /* always leave first 512 bytes to simulate mbr partitioning table */

#pragma pack(push, 1)
struct FatBootSector{
    uint8_t jmp[3];
    uint8_t oem[8];
    uint8_t bps[2]; /* byte per sector */
    uint8_t spc;   /*secotr per cluster */
    uint8_t rsv_sector[2]; /* number of reserved sectors */
    uint8_t num_fat;   /* number of fat table */
    uint8_t num_rootdir[2]; /* number of root director entries, must be round off to bps */
    uint8_t num_sect[2]; /* number of sector, 0 if more than 2 bytes */
    uint8_t desc_type;  /* media descriptor type */
    uint8_t num_sect_fat[2]; /* number of sector per fat */
    uint8_t num_sect_per_track[2];
    uint8_t num_head[2];
    uint8_t num_hidden_sector[4];
    uint8_t sector_count[4];
    uint8_t drive_num;
    uint8_t window_flag;
    uint8_t boot_sig;
    uint8_t volume_id[4];
    uint8_t label[11]; /* volume label name */
    uint8_t sys_ident[8];
    uint8_t bootcode[448];
    uint8_t signature[2];
};
#pragma pack(pop)

struct OpenFile {
    uint8_t drive_num;
    char name[8];
    char ext[3];
    uint32_t size;          // total size in bytes
    uint32_t pos;           // current file pointer
    uint16_t first_cluster; // starting cluster in FAT16
}; 


class Fat16Fs{
    public:
        Fat16Fs(DiskImg& disk); /* accept string size 2K, 2M, 2G */
        ~Fat16Fs();
        DiskStatus fat_format(long long bytes);
        void fat_write(int fd, std:: byte* data, int size);
        int fat_open(std::string filename, int mode);
        std::byte * fat_read(int fd, std::byte & buf, int size);
        int fat_seek(int fd, int offset, int whenece);
        std::vector<std::string> fat_listdir();
        bool fat_delete(std::string& filename);
        bool fat_create(std::string& filename, bool overwrite);
        bool fat_mkdir(std::string& dirname);
        bool fat_chdir(std::string);
        bool fat_close(int fd);
        DiskStatus fat_write(std::string filename);
        std::string get_cwd();

    private:
        struct FatBootSector bpb;
        std::unordered_map<int, struct OpenFile> open_files;
        int allocate_cluster();
        int free_cluster_chain(uint16_t cluster_start);
        int cluster_to_sector();
        DiskStatus format_fat_table();
        DiskStatus write_fat16_boot(size_t bytes);
        DiskStatus format_dir_entries();
        DiskImg& Disk;
        size_t sector_per_cluster;
        size_t data_area_start;
        size_t data_area_sectors;
        size_t total_sector;
        size_t total_cluster;
        size_t root_dir_sector_start;
        size_t root_dir_sectors;
        size_t fat_table_sector_start;
        size_t fat_sectors;
        size_t part_start;
        std::string cwd;
};

#endif
