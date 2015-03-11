/* This is a quick&dirty ACPI shutdown implementation from:
 * http://forum.osdev.org/viewtopic.php?t=16990
 *
 * ProgOS students: Feel free to submit cleanup patches.
 */
#include "devices/acpi.h"
#include <console.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include "devices/kbd.h"
#include "devices/serial.h"
#include "devices/timer.h"
#include "threads/io.h"
#include "threads/thread.h"
#ifdef USERPROG
#include "userprog/exception.h"
#endif
#ifdef FILESYS
#include "devices/block.h"
#include "filesys/filesys.h"
#endif


static uint32_t *SMI_CMD;
static uint8_t ACPI_ENABLE;
static uint8_t ACPI_DISABLE;
static uint16_t PM1a_CNT;
static uint16_t PM1b_CNT;
static uint32_t SLP_TYPa;
static uint32_t SLP_TYPb;
static uint32_t SLP_EN;
static uint32_t SCI_EN;
static uint8_t PM1_CNT_LEN;

struct RSDPtr {
  uint8_t signature[8];
  uint8_t checksum;
  uint8_t oemid[6];
  uint8_t revision;
  uint32_t *rsdt_address;
} __attribute__ ((packed));

struct FACP {
  uint8_t signature[4];
  uint32_t length;
  uint8_t unneeded1[40 - 8];
  uint32_t *DSDT;
  uint8_t unneeded2[48 - 44];
  uint32_t *SMI_CMD;
  uint8_t ACPI_ENABLE;
  uint8_t ACPI_DISABLE;
  uint8_t unneded3[64 - 54];
  uint32_t PM1a_CNT_BLK;
  uint32_t PM1b_CNT_BLK;
  uint8_t unneeded4[89 - 72];
  uint8_t PM1_CNT_LEN;
} __attribute__ ((packed));

// check if the given address has a valid header
static uint32_t *
acpi_check_RSD_ptr (unsigned int *ptr)
{
  char *sig = "RSD PTR ";
  struct RSDPtr *rsdp = (struct RSDPtr *) ptr;
  uint8_t *bptr;
  uint8_t check = 0;
  size_t i;

  if (memcmp (sig, rsdp, 8) == 0) {
    // check checksum rsdpd
    bptr = (uint8_t *) ptr;
    for (i = 0; i < sizeof (struct RSDPtr); ++i) {
      check += *bptr;
      bptr++;
    }

    // found valid rsdpd   
    if (check == 0) {
      return (uint32_t *) rsdp->rsdt_address;
    }
  }

  return NULL;
}



// finds the acpi header and returns the address of the rsdt
static uint32_t *
acpi_get_RSD_ptr (void)
{
  unsigned int *addr;
  unsigned int *rsdp;

  // search below the 1mb mark for RSDP signature
  for (addr = (uint32_t *) 0x000E0000; (uint32_t) addr < 0x00100000;
      addr += 0x10 / sizeof (addr)) {
    rsdp = acpi_check_RSD_ptr (addr);
    if (rsdp != NULL) {
      return rsdp;
    }
  }

  // at address 0x40:0x0E is the RM segment of the ebda
  int ebda = *((uint16_t *) 0x40E);	// get pointer
  ebda = ebda * 0x10 & 0x000FFFFF;	// transform segment into linear address

  // search Extended BIOS Data Area for the Root System Description Pointer signature
  for (addr = (uint32_t *) ebda; (int) addr < ebda + 1024;
      addr += 0x10 / sizeof (addr)) {
    rsdp = acpi_check_RSD_ptr (addr);
    if (rsdp != NULL)
      return rsdp;
  }

  return NULL;
}



// checks for a given header and validates checksum
static int
acpi_check_header (uint32_t * ptr, char *sig)
{
  if (memcmp (ptr, sig, 4) == 0) {
    char *check_ptr = (char *) ptr;
    int len = *(ptr + 1);
    char check = 0;
    while (0 < len--) {
      check += *check_ptr;
      check_ptr++;
    }
    if (check == 0)
      return 0;
  }
  return -1;
}

static int
acpi_enable (void)
{
  // check if acpi is enabled
  if ((inw ((unsigned int) PM1a_CNT) & SCI_EN) == 0) {
    // check if acpi can be enabled
    if (SMI_CMD != 0 && ACPI_ENABLE != 0) {
      outb ((unsigned int) SMI_CMD, ACPI_ENABLE);	// send acpi enable command
      // give 3 seconds time to enable acpi
      int i;
      for (i = 0; i < 300; i++) {
        if ((inw ((unsigned int) PM1a_CNT) & SCI_EN) == 1)
          break;
        timer_mdelay (10);
      }
      if (PM1b_CNT != 0)
        for (; i < 300; i++) {
          if ((inw ((unsigned int) PM1b_CNT) & SCI_EN) == 1)
            break;
          timer_mdelay (10);
        }
      if (i < 300) {
        printf ("enabled acpi.\n");
        return 0;
      } else {
        printf ("couldn't enable acpi.\n");
        return -1;
      }
    } else {
      printf ("no known way to enable acpi.\n");
      return -1;
    }
  } else {
    return 0;
  }
}

//
// bytecode of the \_S5 object
// -----------------------------------------
//        | (optional) |    |    |    |   
// NameOP | \          | _  | S  | 5  | _
// 08     | 5A         | 5F | 53 | 35 | 5F
//
// -----------------------------------------------------------------------------------------------------------
//           |           |              | ( SLP_TYPa   ) | ( SLP_TYPb   ) | ( Reserved   ) | (Reserved    )
// PackageOP | PkgLength | NumElements  | byteprefix Num | byteprefix Num | byteprefix Num | byteprefix Num
// 12        | 0A        | 04           | 0A         05  | 0A          05 | 0A         05  | 0A         05
//
//----this-structure-was-also-seen----------------------
// PackageOP | PkgLength | NumElements |
// 12        | 06        | 04          | 00 00 00 00
//
// (Pkglength bit 6-7 encode additional PkgLength bytes [shouldn't be the case here])
//
int
acpi_init (void)
{
  uint32_t *ptr = acpi_get_RSD_ptr ();

  // check if address is correct  ( if acpi is available on this pc )
  if (ptr != NULL && acpi_check_header (ptr, "RSDT") == 0) {
    // the RSDT contains an unknown number of pointers to acpi tables
    int32_t entrys = *(ptr + 1);
    entrys = (entrys - 36) / 4;
    ptr += 36 / 4;		// skip header information

    while (0 < entrys--) {
      // check if the desired table is reached
      if (acpi_check_header ((uint32_t *) * ptr, "FACP") == 0) {
        entrys = -2;
        struct FACP *facp = (struct FACP *) *ptr;
        if (acpi_check_header (facp->DSDT, "DSDT") == 0) {
          // search the \_S5 package in the DSDT
          char *s5_addr = (char *) facp->DSDT + 36;	// skip header
          int dsdtLength = *(facp->DSDT + 1) * 4 - 36;
          while (0 < dsdtLength--) {
            if (memcmp (s5_addr, "_S5_", 4) == 0)
              break;
            s5_addr++;
          }
          // check if \_S5 was found
          if (dsdtLength > 0) {
            // check for valid AML structure
            if ((*(s5_addr - 1) == 0x08
                  || (*(s5_addr - 2) == 0x08
                    && *(s5_addr - 1) == '\\'))
                && *(s5_addr + 4) == 0x12) {
              s5_addr += 5;
              s5_addr += ((*s5_addr & 0xC0) >> 6) + 2;	// calculate PkgLength size

              if (*s5_addr == 0x0A)
                s5_addr++;	// skip byteprefix
              SLP_TYPa = *(s5_addr) << 10;
              s5_addr++;

              if (*s5_addr == 0x0A)
                s5_addr++;	// skip byteprefix
              SLP_TYPb = *(s5_addr) << 10;

              SMI_CMD = facp->SMI_CMD;

              ACPI_ENABLE = facp->ACPI_ENABLE;
              ACPI_DISABLE = facp->ACPI_DISABLE;

              PM1a_CNT = facp->PM1a_CNT_BLK;
              PM1b_CNT = facp->PM1b_CNT_BLK;

              PM1_CNT_LEN = facp->PM1_CNT_LEN;

              SLP_EN = 1 << 13;
              SCI_EN = 1;

              return 0;
            } else {
              printf ("\\_S5 parse error.\n");
            }
          } else {
            printf ("\\_S5 not present.\n");
          }
        } else {
          printf ("DSDT invalid.\n");
        }
      }
      ptr++;
    }
    printf ("no valid FACP present.\n");
  } else {
    printf ("no acpi.\n");
  }

  return -1;
}

void
acpi_poweroff (void)
{
  // SCI_EN is set to 1 if acpi shutdown is possible
  if (SCI_EN == 0)
    return;

  acpi_enable ();

  // send the shutdown command
  // printf("outw(0x%08x, 0x%08x | 0x%08x)\n", PM1a_CNT, SLP_TYPa, SLP_EN);
  outw ((uint16_t) PM1a_CNT, SLP_TYPa | SLP_EN);
  if (PM1b_CNT != 0)
    outw ((uint16_t) PM1b_CNT, SLP_TYPb | SLP_EN);

  printf ("acpi poweroff failed.\n");
}
