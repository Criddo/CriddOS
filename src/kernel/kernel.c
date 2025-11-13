/* kernel.c - Main kernel implementation with text editor, calculator, and FAT16 filesystem */

#include <stdint.h>
#include <stddef.h>
#include "editor.h"
#include "calc.h"

/* ============================================================================
   VGA TEXT MODE DEFINITIONS
   ============================================================================ */
#define VGA_BUF ((volatile uint16_t*)0xB8000)  // VGA text buffer memory address
#define VGA_WIDTH 80                            // Characters per line
#define VGA_HEIGHT 25                           // Lines on screen
#define VGA_ATTR 0x07                          // Default attribute: light gray on black

/* ============================================================================
   EXTERNAL ASSEMBLY FUNCTIONS
   ============================================================================ */
extern void init_idt64(void);  // Initialise the Interrupt Descriptor Table (64-bit)

/* ============================================================================
   INTERRUPT HANDLERS
   ============================================================================ */
void handle_scancode(uint8_t scancode); //Called from assembly ISR wrapper when keyboard interrupt

/* ============================================================================
   VGA OUTPUT STATE
   ============================================================================ */
// Cursor tracking for text output
static size_t krow = 0;  // Current row position (0-24)
static size_t kcol = 0;  // Current column position (0-79)

/* ============================================================================
   SCREEN MANAGEMENT FUNCTIONS
   ============================================================================ */
/* Clear the entire screen by writing spaces with default attributes */
static void kclear(void) {
    for (size_t r = 0; r < VGA_HEIGHT; ++r) {
        for (size_t c = 0; c < VGA_WIDTH; ++c) {
            // Each VGA cell is 16 bits: low byte = character, high byte = attribute
            VGA_BUF[r * VGA_WIDTH + c] = (uint16_t)(' ') | ((uint16_t)VGA_ATTR << 8);
        }
    }
    krow = 0;  // Reset cursor to top-left
    kcol = 0;
}

/* Draw a character at a specific position with custom attribute */
static void kdraw_char(size_t row, size_t col, char c, uint8_t attr) {
    if (row < VGA_HEIGHT && col < VGA_WIDTH) {
        // Combine character and attribute into single 16-bit value
        VGA_BUF[row * VGA_WIDTH + col] = (uint16_t)c | ((uint16_t)attr << 8);
    }
}

/* Output a single character to the screen at current cursor position */
static void kputchar(char c) {
    if (c == '\n') {
        // Newline: move to start of next row
        kcol = 0;
        krow++;
        if (krow >= VGA_HEIGHT) krow = 0;  // Wrap to top if at bottom
        return;
    } else if (c == '\r') {
        // Carriage return: move to start of current row
        kcol = 0;
        return;
    } else if (c == '\b') {
        // Backspace: move back one position and clear character
        if (kcol > 0) {
            kcol--;
            VGA_BUF[krow * VGA_WIDTH + kcol] = (uint16_t)(' ') | ((uint16_t)VGA_ATTR << 8);
        }
        return;
    }

    // Write character at current position
    VGA_BUF[krow * VGA_WIDTH + kcol] = (uint16_t)c | ((uint16_t)VGA_ATTR << 8);
    kcol++;

    // Handle line wrapping
    if (kcol >= VGA_WIDTH) {
        kcol = 0;
        krow++;
        if (krow >= VGA_HEIGHT) krow = 0;
    }
}

/* Print a null-terminated string */
static void kprints(const char *s) {
    while (*s) {
        kputchar(*s++);
    }
}

/* ============================================================================
   I/O PORT ACCESS FUNCTIONS
   ============================================================================ */
// Low-level functions to read/write CPU I/O ports

/* Read a byte from an I/O port */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* Write a byte to an I/O port */
static inline void outb(uint16_t port, uint8_t val) {
    asm volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* Read a word (16 bits) from an I/O port */
static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    asm volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* Write a word (16 bits) to an I/O port */
static inline void outw(uint16_t port, uint16_t val) {
    asm volatile ("outw %0, %1" : : "a"(val), "Nd"(port));
}

/* ============================================================================
   ATA/IDE HARD DISK DRIVER (PIO MODE)
   ============================================================================
// Allows non-volatile permanent storage (vs RAM storage which is volatile)

/* ATA I/O Port Definitions */
#define ATA_DATA_PORT      0x1F0  // Data register (16-bit)
#define ATA_ERROR_PORT     0x1F1  // Error register (read) / Features (write)
#define ATA_SECT_COUNT     0x1F2  // Sector count register
#define ATA_LBA_LOW        0x1F3  // LBA bits 0-7
#define ATA_LBA_MID        0x1F4  // LBA bits 8-15
#define ATA_LBA_HIGH       0x1F5  // LBA bits 16-23
#define ATA_DRIVE_HEAD     0x1F6  // Drive/Head register (contains LBA bits 24-27)
#define ATA_STATUS         0x1F7  // Status register (read)
#define ATA_COMMAND        0x1F7  // Command register (write)
#define ATA_CONTROL        0x3F6  // Device control / Alternate status

/* ATA Status Register Bits */
#define ATA_SR_BSY  0x80  // Busy - drive is processing command
#define ATA_SR_DRDY 0x40  // Drive Ready
#define ATA_SR_DF   0x20  // Drive Fault
#define ATA_SR_DRQ  0x08  // Data Request - ready for data transfer
#define ATA_SR_ERR  0x01  // Error - check error register for details

/* Small I/O wait: reading alternate status provides ~400ns delay */
static inline void io_wait(void) {
    (void)inb(ATA_CONTROL);  // Read alternate status 4 times
    (void)inb(ATA_CONTROL);  // Each read takes ~100ns
    (void)inb(ATA_CONTROL);
    (void)inb(ATA_CONTROL);
}

/* Wait for ATA drive to be ready. If non-zero, also wait for DRQ (data request) flag */
static int ata_wait(uint32_t want_drq) {
    /* Poll status register with timeout */
    for (int i = 0; i < 1000000; ++i) {
        uint8_t st = inb(ATA_STATUS);  // Read status register

        // Check for error condition
        if (st & ATA_SR_ERR) {
            kprints("ATA: status ERR during poll\n");
            return -1; //-1 on error
        }

        // If busy, continue waiting
        if (st & ATA_SR_BSY) continue;

        // If not busy anymore
        if (want_drq) {
            // If DRQ needed, check if it's set
            if (st & ATA_SR_DRQ) return 0;
            else continue;
        } else {
            // Just needed not-busy
            return 0; //return 0 on success
        }
    }
    kprints("ATA: poll timeout\n");
    return -1; //-1 on timeout
}

/* Select ATA drive (master=0, slave=1) with proper delays */
static inline void ata_select_drive(uint8_t drive) {
    // Set LBA mode (bit 7) and drive select bit (bit 4)
    uint8_t val = 0xE0 | ((drive & 1) << 4);
    outb(ATA_DRIVE_HEAD, val);

    // Wait 400ns for drive selection to take effect
    io_wait();
    io_wait();
    (void)inb(ATA_CONTROL);  // Final alternate status read to flush
}

/* Read one 512-byte sector from disk using LBA28 (Logical Blcok Address) */
static int ata_read_sector(uint32_t lba, void *buf) {
    // LBA28 supports up to 2^28 sectors
    if (lba > 0x0FFFFFFF) return -1;

    /* Wait for drive to be ready */
    (void)inb(ATA_STATUS);  // Dummy read to flush any pending status
    if (ata_wait(0) != 0) {
        kprints("ATA: busy wait failed before read\n");
        return -1;
    }

    /* Set drive and upper 4 bits of LBA (bits 24-27) */
    outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    io_wait();
    (void)inb(ATA_STATUS);  // Give device time to process

    /* Set sector count to 1 */
    outb(ATA_SECT_COUNT, 1);

    /* Set LBA low, middle, and high bytes (bits 0-23) */
    outb(ATA_LBA_LOW, (uint8_t)(lba & 0xFF));           // LBA bits 0-7
    outb(ATA_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));    // LBA bits 8-15
    outb(ATA_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));  // LBA bits 16-23
    io_wait();  // Small delay after register writes

    /* Issue READ SECTORS command (0x20) */
    outb(ATA_COMMAND, 0x20);

    /* Wait for DRQ (data request) to be set */
    io_wait();
    if (ata_wait(1) != 0) {
        kprints("ATA: DRQ not set after READ command\n");
        return -1;
    }

    /* Read 256 words (512 bytes) from data port */
    uint16_t *dst = (uint16_t *)buf;
    for (int i = 0; i < 256; ++i) {
        dst[i] = inw(ATA_DATA_PORT);
    }

    /* Wait for BSY to clear after data transfer */
    if (ata_wait(0) != 0) {
        kprints("ATA: final BSY wait failed after data read\n");
        return -1;
    }

    return 0; //return 0 on success
}

/* Write one 512-byte sector to disk using LBA28 ((Logical Block Addressing) */
static int ata_write_sector(uint32_t lba, const void *buf) {
    // Validate LBA is within LBA28 range
    if (lba > 0x0FFFFFFF) return -1;

    /* Wait for drive to be ready */
    (void)inb(ATA_STATUS);
    if (ata_wait(0) != 0) {
        kprints("ATA: busy wait failed before write\n");
        return -1;
    }

    /* Set drive and upper 4 bits of LBA */
    outb(ATA_DRIVE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));
    io_wait();
    (void)inb(ATA_STATUS);

    /* Set sector count and LBA */
    outb(ATA_SECT_COUNT, 1);  // Write one sector
    outb(ATA_LBA_LOW, (uint8_t)(lba & 0xFF));           // Extract bits 0-7
    outb(ATA_LBA_MID, (uint8_t)((lba >> 8) & 0xFF));    // Extract bits 8-15
    outb(ATA_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));  // Extract bits 16-23
    io_wait();

    /* Issue WRITE SECTORS command (0x30) */
    outb(ATA_COMMAND, 0x30);

    /* Wait for DRQ to indicate drive is ready for data */
    if (ata_wait(1) != 0) {
        kprints("ATA: DRQ not set after WRITE command\n");
        return -1;
    }

    /* Write 256 words (512 bytes) to data port */
    const uint16_t *src = (const uint16_t *)buf; //buffer containing 512 bytes to write
    for (int i = 0; i < 256; ++i) {
        outw(ATA_DATA_PORT, src[i]);
    }

    /* Wait for write to complete */
    if (ata_wait(0) != 0) {
        kprints("ATA: final BSY wait failed after data write\n");
        return -1;
    }

    /* Flush disk cache with CACHE FLUSH command (0xE7) */
    outb(ATA_COMMAND, 0xE7);
    if (ata_wait(0) != 0) {
        kprints("ATA: cache flush failed\n");
        return -1;
    }

    return 0; //return 0 on success
}

/* ============================================================================
   FAT16 FILESYSTEM IMPLEMENTATION
   ============================================================================ */
 // FAT16 filesystem for persistent storage
 // Layout: Boot sector | FAT tables | Root directory | Data area

/* Filesystem Parameters */
#define SECTOR_SIZE 512           // Standard sector size
#define TOTAL_SECTORS 512         // Total disk size: 256KB

#define BYTES_PER_SECTOR 512      // Bytes per sector
#define SECTORS_PER_CLUSTER 1     // Cluster size = 512 bytes
#define RESERVED_SECTORS 1        // Boot sector
#define NUM_FATS 2                // Two FAT copies for redundancy
#define ROOT_DIR_ENTRIES 512      // Max files in root directory
#define SECTORS_PER_FAT 4         // Size of each FAT table

/* Calculate number of sectors occupied by root directory */
static uint32_t root_dir_sectors(void) {
    // Each directory entry is 32 bytes
    return ((ROOT_DIR_ENTRIES * 32) + (BYTES_PER_SECTOR - 1)) / BYTES_PER_SECTOR;
}

/* Calculate first sector of data area (where file contents are stored) */
static uint32_t first_data_sector(void) {
    return RESERVED_SECTORS + (NUM_FATS * SECTORS_PER_FAT) + root_dir_sectors();
}

/* Calculate first sector of FAT table */
static uint32_t first_fat_sector(void) {
    return RESERVED_SECTORS;
}

/* Read a sector with bounds checking */
static void read_sector(uint32_t sec, void *buf) {
    // Return 0 if sector is out of bounds
    if (sec >= TOTAL_SECTORS) {
        uint8_t *dst = (uint8_t *)buf;
        for (size_t i = 0; i < SECTOR_SIZE; ++i) dst[i] = 0;
        return;
    }
    // Attempt to read, return 0 on failure
    if (ata_read_sector(sec, buf) != 0) {
        uint8_t *dst = (uint8_t *)buf;
        for (size_t i = 0; i < SECTOR_SIZE; ++i) dst[i] = 0;
    }
}

/* Write a sector with bounds checking */
static void write_sector(uint32_t sec, const void *buf) {
    if (sec >= TOTAL_SECTORS) return;  // Ignore out-of-bounds writes
    ata_write_sector(sec, buf);
}

/* Initialise FAT16 filesystem by creating boot sector and FAT tables */
static void fat16_init(void) {
    /* Zero out all sectors on disk */
    uint8_t zero[SECTOR_SIZE];
    for (size_t i = 0; i < SECTOR_SIZE; ++i) zero[i] = 0;
    for (uint32_t s = 0; s < TOTAL_SECTORS; ++s) {
        write_sector(s, zero);
    }

    /* Create Boot Parameter Block (BPB) in sector 0 */
    uint8_t bpb[SECTOR_SIZE];
    for (size_t i = 0; i < SECTOR_SIZE; ++i) bpb[i] = 0;

    // Jump instruction and NOP
    bpb[0] = 0xEB; bpb[1] = 0x3C; bpb[2] = 0x90;

    // OEM identifier (8 bytes)
    const char *oem = "ATAFAT16";
    for (size_t i = 0; i < 8; ++i) bpb[3+i] = (i < 8) ? oem[i] : ' ';

    // Bytes per sector (little-endian format, stores the least-significant byte at the smallest address)
    bpb[11] = (BYTES_PER_SECTOR & 0xFF);
    bpb[12] = (BYTES_PER_SECTOR >> 8) & 0xFF;

    // Sectors per cluster
    bpb[13] = SECTORS_PER_CLUSTER;

    // Reserved sectors (little-endian)
    bpb[14] = (RESERVED_SECTORS & 0xFF);
    bpb[15] = (RESERVED_SECTORS >> 8) & 0xFF;

    // Root directory entries (little-endian)
    bpb[16] = ROOT_DIR_ENTRIES & 0xFF;
    bpb[17] = (ROOT_DIR_ENTRIES >> 8) & 0xFF;

    // Total sectors (16-bit, little-endian)
    uint16_t totsec16 = (TOTAL_SECTORS <= 0xFFFF) ? (uint16_t)TOTAL_SECTORS : 0;
    bpb[19] = (totsec16 & 0xFF);
    bpb[20] = (totsec16 >> 8) & 0xFF;

    // Media descriptor (0xF8 = fixed disk)
    bpb[21] = 0xF8;

    // Sectors per FAT (little-endian)
    bpb[22] = SECTORS_PER_FAT & 0xFF;
    bpb[23] = (SECTORS_PER_FAT >> 8) & 0xFF;

    // Sectors per track, heads, hidden sectors (zeros for simplicity)
    bpb[24] = 0; bpb[25] = 0;
    bpb[26] = 0; bpb[27] = 0;
    bpb[28] = 0; bpb[29] = 0; bpb[30] = 0; bpb[31] = 0;

    // Extended boot signature
    bpb[38] = 0x29;

    // Volume label (11 bytes, space-padded)
    const char *label = "ATADISK    ";
    for (size_t i = 0; i < 11; ++i) bpb[43 + i] = label[i];

    // Filesystem type (8 bytes, space-padded)
    const char *fs = "FAT16   ";
    for (size_t i = 0; i < 8; ++i) bpb[54 + i] = fs[i];

    // Write boot sector
    write_sector(0, bpb);

    /* Initialise both FAT tables */
    uint8_t fatsec[SECTOR_SIZE];
    for (size_t s = 0; s < SECTORS_PER_FAT; ++s) {
        // Zero out FAT sector
        for (size_t i = 0; i < SECTOR_SIZE; ++i) fatsec[i] = 0;

        // First FAT sector has special media descriptor entries
        if (s == 0) {
            fatsec[0] = 0xF8; fatsec[1] = 0xFF;  // Media descriptor
            fatsec[2] = 0xFF; fatsec[3] = 0xFF;  // End of chain marker
        }

        // Write to both FAT copies
        write_sector(first_fat_sector() + s, fatsec);
        write_sector(first_fat_sector() + s + SECTORS_PER_FAT, fatsec);
    }
}

/* Read a FAT entry (cluster chain link) */
static uint16_t fat_get_entry(uint16_t cluster) {
    // Each FAT16 entry is 2 bytes
    uint32_t fat_offset = (uint32_t)cluster * 2u;

    // Determine which FAT sector contains this entry
    uint32_t sec = first_fat_sector() + (fat_offset / SECTOR_SIZE);
    uint32_t off = fat_offset % SECTOR_SIZE;

    uint8_t secbuf[SECTOR_SIZE];
    read_sector(sec, secbuf);

    // Handle case where entry spans two sectors
    if (off == SECTOR_SIZE - 1) {
        uint8_t nextsecbuf[SECTOR_SIZE];
        read_sector(sec + 1, nextsecbuf);
        return (uint16_t)(secbuf[off] | (nextsecbuf[0] << 8)); //return next cluster in chain
    } else {
        // Entry is within single sector
        uint16_t val = secbuf[off] | (secbuf[off+1] << 8);
        return val; //return 0xFFFF for end-of-chain
    }
}

/* Write a FAT entry (update cluster chain link) */
static void fat_set_entry(uint16_t cluster, uint16_t val) {
    // Calculate FAT entry location
    uint32_t fat_offset = (uint32_t)cluster * 2u;
    uint32_t sec = first_fat_sector() + (fat_offset / SECTOR_SIZE);
    uint32_t off = fat_offset % SECTOR_SIZE;

    // Update both FAT copies
    for (int copy = 0; copy < NUM_FATS; ++copy) {
        uint8_t secbuf[SECTOR_SIZE];
        read_sector(sec + copy * SECTORS_PER_FAT, secbuf);

        // Write low byte
        secbuf[off] = val & 0xFF; //val: Value to write (next cluster or end-of-chain marker)

        // Handle entry spanning two sectors
        if (off == SECTOR_SIZE - 1) {
            write_sector(sec + copy * SECTORS_PER_FAT, secbuf);
            uint8_t nextsecbuf[SECTOR_SIZE];
            read_sector(sec + 1 + copy * SECTORS_PER_FAT, nextsecbuf);
            nextsecbuf[0] = (val >> 8) & 0xFF;
            write_sector(sec + 1 + copy * SECTORS_PER_FAT, nextsecbuf);
        } else {
            // Write high byte
            secbuf[off+1] = (val >> 8) & 0xFF;
            write_sector(sec + copy * SECTORS_PER_FAT, secbuf);
        }
    }
}

/* Convert cluster number to disk sector number */
static uint32_t cluster_to_sector(uint16_t cluster) {
    // Cluster 2 is the first data cluster
    return first_data_sector() + (cluster - 2) * SECTORS_PER_CLUSTER;
}

/* Find an unused cluster in the FAT memory */
static int16_t fat_find_free_cluster(void) {
    // Calculate how many data clusters are available
    uint32_t data_sectors = TOTAL_SECTORS - first_data_sector();
    uint32_t max_clusters = data_sectors / SECTORS_PER_CLUSTER;

    // Scan FAT for free entry (value 0x0000)
    for (uint16_t c = 2; c < 2 + max_clusters; ++c) {
        uint16_t v = fat_get_entry(c);
        if (v == 0x0000) return (int16_t)c;
    }
    return -1;  // No free clusters, disk full
}

/* Convert filename to DOS 8.3 format (space-padded) */
static void make_dos_name(const char *in, uint8_t out[11]) {
    // Initialise with spaces
    for (int i = 0; i < 11; ++i) out[i] = ' ';

    // Copy filename part (up to 8 characters)
    int i = 0, j = 0;
    while (*in && *in != '.' && j < 8) {
        char c = *in++;
        if (c >= 'a' && c <= 'z') c -= 32;  // Convert to uppercase
        out[j++] = c;
    }

    // Skip dot if present
    if (*in == '.') in++;

    // Copy extension part (up to 3 characters)
    j = 8;
    while (*in && j < 11) {
        char c = *in++;
        if (c >= 'a' && c <= 'z') c -= 32;  // Convert to uppercase
        out[j++] = c;
    }
}

/* ============================================================================
   FAT16 FILE WRITING
   ============================================================================ */
static int fat16_write_file(const char *name, const uint8_t *data, size_t len) {
    // Convert filename to DOS 8.3 format
    uint8_t dosname[11];
    make_dos_name(name, dosname);

    /* Search root directory for existing file or free entry */
    uint8_t dirsec[SECTOR_SIZE];
    uint32_t root_start = RESERVED_SECTORS + (NUM_FATS * SECTORS_PER_FAT);
    uint32_t root_sectors = root_dir_sectors();
    int found_offset = -1;  // Directory entry location
    uint16_t existing_start_cluster = 0;

    // Scan root directory
    for (uint32_t s = 0; s < root_sectors; ++s) {
        read_sector(root_start + s, dirsec);

        // Each directory entry is 32 bytes
        for (uint32_t off = 0; off < SECTOR_SIZE; off += 32) {
            if (dirsec[off] == 0x00) {
                // Empty entry (end of directory)
                if (found_offset < 0) found_offset = (int)((s * SECTOR_SIZE) + off);
                break;
            } else if (dirsec[off] == 0xE5) {
                // Deleted entry (can be reused)
                if (found_offset < 0) found_offset = (int)((s * SECTOR_SIZE) + off);
                continue;
            } else {
                // Check if this entry matches our filename
                int match = 1;
                for (int k = 0; k < 11; ++k) {
                    if (dirsec[off+k] != dosname[k]) { match = 0; break; }
                }
                if (match) {
                    // Found existing file - will overwrite
                    found_offset = (int)((s * SECTOR_SIZE) + off);
                    existing_start_cluster = (uint16_t)(dirsec[off+26] | (dirsec[off+27] << 8));
                    s = root_sectors;  // Exit outer loop
                    break;
                }
            }
        }
    }

    /* Free existing clusters if file already exists */
    if (existing_start_cluster >= 2) {
        uint16_t c = existing_start_cluster;
        while (c >= 2 && c < 0xFFF8) {
            uint16_t next = fat_get_entry(c);
            fat_set_entry(c, 0x0000);  // Mark cluster as free
            if (next >= 0xFFF8) break;  // End of chain
            c = next;
        }
    }

    /* Allocate clusters and write file data */
    uint16_t first_cluster = 0, prev_cluster = 0;  // Track first and previous cluster numbers
    size_t remaining = len;                         // Bytes remaining to write
    const uint8_t *ptr = data;                      // Pointer to current position in data buffer

    if (len > 0) {
        // Loop through data, allocating one cluster per iteration
        while (remaining > 0) {
            // Find an available cluster in the FAT
            int16_t c = fat_find_free_cluster();
            if (c < 0) return -1;  // Fails if no free clusters available

            // If this is the first cluster, remember it for the directory entry
            if (!first_cluster) first_cluster = (uint16_t)c;

            // Link previous cluster to this one (building the cluster chain)
            if (prev_cluster) fat_set_entry(prev_cluster, (uint16_t)c);
            prev_cluster = (uint16_t)c;  // Update previous cluster tracker

            // Create a sector buffer and initialise to zero
            uint8_t secbuf[SECTOR_SIZE];
            for (size_t i = 0; i < SECTOR_SIZE; ++i) secbuf[i] = 0;

            // Calculate how many bytes to copy (one sector or remaining, whichever is smaller)
            size_t tocopy = remaining > SECTOR_SIZE ? SECTOR_SIZE : remaining;

            // Copy data from input buffer to sector buffer
            for (size_t i = 0; i < tocopy; ++i) secbuf[i] = *ptr++;
            remaining -= tocopy;  // Update remaining byte count

            // Write the sector to disk at this cluster's location
            write_sector(cluster_to_sector((uint16_t)c), secbuf);
        }

        // Mark the last cluster with end-of-file marker (0xFFFF)
        if (prev_cluster) fat_set_entry(prev_cluster, 0xFFFF);
    }

    /* Update the directory entry with file metadata */
    if (found_offset < 0) return -1;  // Safety check

    // Calculate which sector and offset within sector the directory entry is at
    uint32_t sidx = found_offset / SECTOR_SIZE;    // Sector index
    uint32_t off = found_offset % SECTOR_SIZE;     // Offset within sector

    // Read the directory sector into buffer
    read_sector(root_start + sidx, dirsec);

    // Write the 8.3 filename (11 bytes)
    for (int k = 0; k < 11; ++k) dirsec[off + k] = dosname[k];

    // Set file attributes and reserved bytes
    dirsec[off + 11] = 0x20;  // Attribute: Archive bit set (normal file)
    dirsec[off + 12] = 0;     // Reserved for Windows NT
    dirsec[off + 13] = 0;     // Creation time tenth of second
    dirsec[off + 14] = 0; dirsec[off + 15] = 0;  // Creation time
    dirsec[off + 16] = 0; dirsec[off + 17] = 0;  // Creation date

    // Write starting cluster number (little-endian, 16-bit)
    dirsec[off + 26] = first_cluster & 0xFF;           // Low byte
    dirsec[off + 27] = (first_cluster >> 8) & 0xFF;    // High byte

    // Write file size (little-endian, 32-bit)
    dirsec[off + 28] = (uint8_t)(len & 0xFF);          // Byte 0 (LSB)
    dirsec[off + 29] = (uint8_t)((len >> 8) & 0xFF);   // Byte 1
    dirsec[off + 30] = (uint8_t)((len >> 16) & 0xFF);  // Byte 2
    dirsec[off + 31] = (uint8_t)((len >> 24) & 0xFF);  // Byte 3 (MSB)

    // Write the updated directory sector back to disk
    write_sector(root_start + sidx, dirsec);

    return 0;  // Success
}

/* ============================================================================
   FAT16 FILE READING
   ============================================================================ */
static int fat16_read_file(const char *name, uint8_t *out, size_t maxlen) {
    // Convert filename to DOS 8.3 format
    uint8_t dosname[11];
    make_dos_name(name, dosname);

    uint8_t dirsec[SECTOR_SIZE];  // Buffer for reading directory sectors

    // Calculate root directory location
    uint32_t root_start = RESERVED_SECTORS + (NUM_FATS * SECTORS_PER_FAT);
    uint32_t root_sectors = root_dir_sectors();

    // Search through root directory for matching filename
    for (uint32_t s = 0; s < root_sectors; ++s) {
        read_sector(root_start + s, dirsec);  // Read one directory sector

        // Each directory entry is 32 bytes; scan through sector
        for (uint32_t off = 0; off < SECTOR_SIZE; off += 32) {
            if (dirsec[off] == 0x00) continue;  // Empty entry (end of directory)
            if (dirsec[off] == 0xE5) continue;  // Deleted entry, skip

            // Compare filename with this entry
            int match = 1;
            for (int k = 0; k < 11; ++k) {
                if (dirsec[off+k] != dosname[k]) {
                    match = 0;
                    break;
                }
            }

            if (match) { // if file found, extract metadata from directory entry

                // Get starting cluster (little-endian, 16-bit at offset 26-27)
                uint16_t start_cluster = (uint16_t)(dirsec[off+26] | (dirsec[off+27] << 8));

                // Get file size (little-endian, 32-bit at offset 28-31)
                uint32_t file_size = (uint32_t)dirsec[off+28] |
                ((uint32_t)dirsec[off+29] << 8) |
                ((uint32_t)dirsec[off+30] << 16) |
                ((uint32_t)dirsec[off+31] << 24);

                // Determine how many bytes to read (file size or buffer limit)
                size_t toread = file_size < maxlen ? file_size : maxlen;
                size_t got = 0;  // Track bytes read so far
                uint16_t c = start_cluster;  // Current cluster in chain

                if (file_size == 0) return 0;  // If empty file

                // Follow the cluster chain and read data
                while (c >= 2 && got < toread) {  // Cluster 0 and 1 are reserved
                    uint8_t secbuf[SECTOR_SIZE];

                    // Read this cluster's data
                    read_sector(cluster_to_sector(c), secbuf);

                    // Calculate how many bytes to copy from this cluster
                    size_t copy = (toread - got > SECTOR_SIZE) ? SECTOR_SIZE : (toread - got);

                    // Copy data to output buffer
                    for (size_t i = 0; i < copy; ++i) out[got + i] = secbuf[i];
                    got += copy;

                    // Get next cluster in chain from FAT
                    uint16_t next = fat_get_entry(c);
                    if (next >= 0xFFF8) break;  // End-of-file marker
                    c = next;
                }

                return (int)got;  // Return number of bytes read
            }
        }
    }

    return -1;  // File not found
}

/* ============================================================================
   KEYBOARD SCANCODE MAPPING
   ============================================================================ */

/* Normal keyboard handling (non-editor) */
static char normal_map[256];  // Maps scancodes to characters (no shift)
static char shift_map[256];   // Maps scancodes to characters (with shift)
static int shift_down = 0;    // Track if shift key is currently pressed
static int ctrl_down = 0;     // Track if control key is currently pressed

static void scancode_map_init(void) {
    // Initialise all mappings to zero (no character)
    for (int i = 0; i < 256; ++i) {
        normal_map[i] = 0;
        shift_map[i] = 0;
    }

    /* Letter keys */
    normal_map[0x10] = 'q'; shift_map[0x10] = 'Q';
    normal_map[0x11] = 'w'; shift_map[0x11] = 'W';
    normal_map[0x12] = 'e'; shift_map[0x12] = 'E';
    normal_map[0x13] = 'r'; shift_map[0x13] = 'R';
    normal_map[0x14] = 't'; shift_map[0x14] = 'T';
    normal_map[0x15] = 'y'; shift_map[0x15] = 'Y';
    normal_map[0x16] = 'u'; shift_map[0x16] = 'U';
    normal_map[0x17] = 'i'; shift_map[0x17] = 'I';
    normal_map[0x18] = 'o'; shift_map[0x18] = 'O';
    normal_map[0x19] = 'p'; shift_map[0x19] = 'P';
    normal_map[0x1E] = 'a'; shift_map[0x1E] = 'A';
    normal_map[0x1F] = 's'; shift_map[0x1F] = 'S';
    normal_map[0x20] = 'd'; shift_map[0x20] = 'D';
    normal_map[0x21] = 'f'; shift_map[0x21] = 'F';
    normal_map[0x22] = 'g'; shift_map[0x22] = 'G';
    normal_map[0x23] = 'h'; shift_map[0x23] = 'H';
    normal_map[0x24] = 'j'; shift_map[0x24] = 'J';
    normal_map[0x25] = 'k'; shift_map[0x25] = 'K';
    normal_map[0x26] = 'l'; shift_map[0x26] = 'L';
    normal_map[0x2C] = 'z'; shift_map[0x2C] = 'Z';
    normal_map[0x2D] = 'x'; shift_map[0x2D] = 'X';
    normal_map[0x2E] = 'c'; shift_map[0x2E] = 'C';
    normal_map[0x2F] = 'v'; shift_map[0x2F] = 'V';
    normal_map[0x30] = 'b'; shift_map[0x30] = 'B';
    normal_map[0x31] = 'n'; shift_map[0x31] = 'N';
    normal_map[0x32] = 'm'; shift_map[0x32] = 'M';

    /* Number keys */
    normal_map[0x02] = '1'; shift_map[0x02] = '!';
    normal_map[0x03] = '2'; shift_map[0x03] = '@';
    normal_map[0x04] = '3'; shift_map[0x04] = '#';
    normal_map[0x05] = '4'; shift_map[0x05] = ';';
    normal_map[0x06] = '5'; shift_map[0x06] = '%';
    normal_map[0x07] = '6'; shift_map[0x07] = '^';
    normal_map[0x08] = '7'; shift_map[0x08] = '&';
    normal_map[0x09] = '8'; shift_map[0x09] = '*';
    normal_map[0x0A] = '9'; shift_map[0x0A] = '(';
    normal_map[0x0B] = '0'; shift_map[0x0B] = ')';

    /* Special keys */
    normal_map[0x0C] = '-'; shift_map[0x0C] = '_';  // Minus/underscore
    normal_map[0x0D] = '='; shift_map[0x0D] = '+';  // Equals/plus
    normal_map[0x1C] = '\n';                        // Enter key
    normal_map[0x39] = ' ';                         // Spacebar

    /* Punctuation keys */
    normal_map[0x27] = ';'; shift_map[0x27] = ':';      // Semicolon/colon
    normal_map[0x28] = '\''; shift_map[0x28] = '"';     // Quote/double-quote
    normal_map[0x2B] = '\\'; shift_map[0x2B] = '|';     // Backslash/pipe
    normal_map[0x33] = ','; shift_map[0x33] = '<';      // Comma/less-than
    normal_map[0x34] = '.'; shift_map[0x34] = '>';      // Period/greater-than
    normal_map[0x35] = '/'; shift_map[0x35] = '?';      // Slash/question mark
    normal_map[0x29] = '`'; shift_map[0x29] = '~';      // Backtick/tilde

    normal_map[0x0E] = '\b';  // Backspace
}

/* ============================================================================
    KEYBOARD INPUT HANDLING
   ============================================================================ */

void handle_scancode(uint8_t scancode) {
    /* If calculator is active, let it handle the scancode */
    if (calc_is_active()) {
        int still_active = calc_handle_scancode(scancode);
        if (!still_active) {
            /* Calculator exited, show welcome message */
            kprints("Exited calculator.\n");
            kprints("Kernel running. Type on keyboard or press Ctrl+E to enter editor or Ctrl+C to enter calculator.\n");
        }
        return;
    }

    /* If editor is active, let it handle the scancode */
    if (editor_is_active()) {
        int still_active = editor_handle_scancode(scancode);
        if (!still_active) {
            /* Editor exited, show welcome message */
            kprints("Exited editor.\n");
            kprints("Kernel running. Type on keyboard or press Ctrl+E to enter editor or Ctrl+C to enter calculator.\n");
        }
        return;
    }

    // Handle shift key press (0x2A = left shift, 0x36 = right shift)
    if (scancode == 0x2A || scancode == 0x36) { shift_down = 1; return; }
    // Handle shift key release (high bit set = release)
    if (scancode == 0xAA || scancode == 0xB6) { shift_down = 0; return; }

    // Handle control key press (0x1D = left control)
    if (scancode == 0x1D) { ctrl_down = 1; return; }
    // Handle control key release
    if (scancode == 0x9D) { ctrl_down = 0; return; }

    // Ignore key release scancodes (high bit set)
    if (scancode & 0x80) return;

    // Map scancode to character based on shift state
    char c = shift_down ? shift_map[scancode] : normal_map[scancode];

    /* Check for Ctrl+E to enter editor */
    if (ctrl_down && (c == 'e' || c == 'E')) {
        editor_start();
        return;
    }

    /* Check for Ctrl+C to enter calculator */
    if (ctrl_down && (c == 'c' || c == 'C')) {
        calc_start();
        return;
    }

    /* Don't output control characters */
    if (ctrl_down) return;

    /* Normal output: display the character if valid */
    if (c) {
        kputchar(c);
    }
}

/* ============================================================================
   SYSTEM INITIALISATION
   ============================================================================ */

static inline void disable_smi(void) {
    // Disable System Management Interrupts by writing to port 0xB2
    outb(0xB2, 0x00);
}

/* ============================================================================
   KERNEL ENTRY POINT
   ============================================================================ */

void kernel_main(void) {
    // Initialise keyboard scancode mapping tables
    scancode_map_init();

    // Disable System Management Interrupts
    disable_smi();

    // Initialise 64-bit Interrupt Descriptor Table
    init_idt64();

    // Print welcome messages
    kprints("Kernel started. If you type on the keyboard, characters will appear below!\n");
    kprints("Press Ctrl-E to enter editor or Ctrl-C to enter calculator.\n");

    // Select ATA drive 0 (primary master)
    ata_select_drive(0);

    // Initialise FAT16 filesystem
    fat16_init();

    /* Initialise editor subsystem and set callbacks */
    editor_init();
    editor_callbacks_t editor_callbacks = {
        .clear_screen = kclear,          // Function to clear the screen
        .draw_char = kdraw_char,         // Function to draw a character
        .fat_write = fat16_write_file,   // Function to write files
        .fat_read = fat16_read_file,     // Function to read files
        .print_message = kprints         // Function to print messages
    };
    editor_set_callbacks(&editor_callbacks);

    /* Initialise calculator subsystem and set callbacks */
    calc_init();
    calc_callbacks_t calc_callbacks = {
        .clear_screen = kclear,      // Function to clear the screen
        .draw_char = kdraw_char      // Function to draw a character
    };
    calc_set_callbacks(&calc_callbacks);

    // Main kernel loop: halt CPU and wait for interrupts
    for (;;) {
        __asm__ volatile ("hlt");  // Halt instruction - CPU sleeps until next interrupt
    }
}
