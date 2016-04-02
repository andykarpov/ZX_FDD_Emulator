// ZX-Spectrum FDD Emulator
//
#define SIDE_SEL  PC0   // pin A0, SIDE SELECT                                  (INPUT)
#define DRIVE_SEL PC1   // pin A1, DRIVE SELECT CONNECT DS0-DS3 using jumper    (INPUT) /// not used yet

#define DIR_SEL   PB0   // pin 8,  DIRECTION SELECT                             (INPUT)
  
#define WRIT_DATA PD0   // pin 0,  WRITE DATA                                   (INPUT) /// not used yet
#define READ_DATA PD1   // pin 1,  READ_DATA                                    (OUTPUT) /// defined in USART
#define STEP      PD2   // pin 2,  STEP                                         (INPUT)
#define WRT_GATE  PD3   // pin 3,  WRITE GATE                                   (INPUT) /// not used yet
#define MOTOR_ON  PD4   // pin 4,  MOTOR ON                                     (INPUT) 
#define INDEX     PD5   // pin 5,  INDEX                                        (OUTPUT)
#define TRK00     PD6   // pin 6,  TRACK 00                                     (OUTPUT)
#define WP        PD7   // pin 7,  WRITE PROTECT                                (OUTPUT)

#include "SDCardModule.h"
#include "Fat32Module.h"
FATFS fat;


/// EMULATOR START -------------------------------------------------------------------------------------------------

#define MAX_CYL 86            /// maximal cylinder supported by FDD
#define MAX_TRACKS MAX_CYL*2  /// maximal track

uint8_t sector_header[64]; // sector header
uint8_t track_header[66]; // track header
uint8_t sector_data[512]; // sector data
uint32_t clust_table[MAX_TRACKS]; // Cluster table
uint32_t cluster_chain[4]; // max cluster chain for 1k cluster = 4;

register volatile uint8_t cylinder asm("r2");
register volatile uint8_t SREG_SAVE asm("r3");
register volatile uint8_t max_cylinder asm("r4");
register volatile uint8_t side asm("r5");

// STATE variable values
// 0-2 - TRACK HEADER
// 3-8 - SECTOR
// 9 - TRACK FOOTER
uint8_t state;
uint8_t sector_byte, prev_byte, second_byte, tmp, b_index, chain_index, chain_size, chain_gen_index;
uint16_t CRC;
volatile uint8_t sector, data_sent, ds2, s_cylinder, s_side;
// FAT32 related vars
uint32_t dsect;
uint16_t fptr_sect, fptr_sect_main;
CLUST curr_clust;

uint8_t MFM_tab[32] = { 0xAA,0xA9,0xA4,0xA5,0x92,0x91,0x94,0x95,0x4A,0x49,0x44,0x45,0x52,0x51,0x54,0x55,0x2A,0x29,0x24,0x25,0x12,0x11,0x14,0x15,0x4A,0x49,0x44,0x45,0x52,0x51,0x54,0x55 };

const uint16_t Crc16Table[256] PROGMEM = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};


///
/// STEP pin interrupt
///////////////////////////////////////////
ISR(INT0_vect, ISR_NAKED)
{
  asm("in r3,0x3F"); // save SREG 
  asm("sbis 0x03, 0"); // check PINB DIR_SEL
  asm("rjmp NEXT_TRACK");
  asm("tst r2");
  asm("breq ZERO");
  asm("dec r2");
  asm("ZERO:");
  asm("rjmp TRK_END");
  asm("NEXT_TRACK:");
  asm("cp r2,r4");
  asm("brge TRK_END");
  asm("inc r2");
  asm("TRK_END:");

  asm ("out 0x3F,r3"); // restore SREG
  reti();    
}

///
/// READ DATA interrupt
///////////////////////////////////////////
ISR(USART_UDRE_vect)
{    
  if (!tmp) // it is ok, as MFM_tab doesn't have zero values
  { 
    // GET DATA BYTE (REAL DATA NOT MFM)
    switch (state)
    {
      case 0: // start track header -----------------------------------------
            // set index LOW
            PORTD &= ~_BV(INDEX);
            //set TRK00 LOW or HIGH
            if (s_cylinder == 0) PORTD &= ~(1 << TRK00); else PORTD |= (1 << TRK00);
            sector_byte = 0x4E;
            b_index = 1;
            state = 1;            
            break;
      case 1: // send track header ------------------------------------------
            if (++b_index != 80) break;
            state = 2;
            b_index = 0;
            break;            
      case 2: // send C2 field ----------------------------------------------
            sector_byte = track_header[b_index++];
            if(sector_byte == 0xC2) second_byte = 0x24;
            if (b_index != 66) break;
            state = 3;
            b_index = 0;
            // set INDEX high
            PORTD |= _BV(INDEX);
            break;      

      case 3: // SECTOR START (ADDERSS FIELD) ------------------------------
            switch (b_index)
            {
              case 0:
                CRC = 0xB230;
                break;          

              case 1:
                CRC = (CRC << 8) ^ pgm_read_word_near(Crc16Table + ((CRC >> 8) ^ s_cylinder));
                break;

              case 2:
                CRC = (CRC << 8) ^ pgm_read_word_near(Crc16Table + ((CRC >> 8) ^ s_side));
                break;

              case 3:
                CRC = (CRC << 8) ^ pgm_read_word_near(Crc16Table + ((CRC >> 8) ^ sector));
                break;

              case 4:
                CRC = (CRC << 8) ^ pgm_read_word_near(Crc16Table + ((CRC >> 8) ^ 1)); // sector size 1 = 256;
                break;
                      
              case 5:
                sector_header[16] = s_cylinder;
                sector_header[17] = s_side;
                break;
                                  
              case 12:
              case 13:
              case 14:
                second_byte = 0x89;
                break;

              case 15:
                sector_header[18] = sector;
                sector_header[20] = (byte)(CRC >> 8);
                sector_header[21] = (byte)CRC;
                /*if(sector == 1 && side == 0 && CRC != 0xFA0C) { 
                  cli();
                  while(1);
                }*/
                break;

              case 56:
              case 57:
              case 58:
                second_byte = 0x89;
                break;
            }
            // send sector bytes before data
            sector_byte = sector_header[b_index++];   // pre-get new byte from buffer
            if (b_index != 60) break;

            b_index = 0;
            state = 4;
            // START GENERATING CRC HERE, PRE-CALC value for A1,A1,A1,FB = 0xE295        
            CRC = 0xE295; // next CRC value
            data_sent = 0;
            break;
        
      case 4: // DATA FIELD -------------------------------------------------        
            // get sector data values
            sector_byte = sector_data[((sector-1) & 1)*256 + b_index++];   // pre-get new byte from buffer
            CRC = (CRC << 8) ^ pgm_read_word_near(Crc16Table + ((CRC >> 8) ^ sector_byte));
            if (b_index != 0) break;
            state = 5;
            break;
        
      case 5: // SECTOR CRC ----------------------------------------------------        
            sector_byte = (byte)(CRC >> 8);
            state = 6;
            break;

      case 6:
            sector_byte = (byte)CRC;
            state = 7;
            break;
      
      case 7: // SECTOR FOOTER -------------------------------------------------
            if (sector <= 15)
            {
              sector++; // increase sector
              data_sent = 1;
              ds2 = 1;
            }
            sector_byte = 0x4E;
            state = 8;
            break;      
      case 8:
            b_index++;
            if (b_index != 56) break;

            if (ds2)
            {
                state = 3;
                b_index = 0;
                ds2 = 0;
                break;
            }
            sector = 1;
            data_sent = 1;

            state = 9;
            b_index = 0;
            break;

      case 9: // TRACK FOOTER -------------------------------------------------
            if (++b_index != 200) break;
            state = 0;
            b_index = 0;        
            break;
    }
    // PRE-FETCH END

    tmp = MFM_tab[sector_byte >> 4]; // get first MFM byte from table    
    if(prev_byte && !(sector_byte & 0x80)) tmp &= 0x7F;
    UDR0 = ~tmp;  // put byte to send buffer
  }
  else
  { // Send second MFM byte
    prev_byte = sector_byte & 1;

    if (second_byte == 0)
      tmp = MFM_tab[sector_byte & 0x1f]; // get second MFM byte from table to "tmp"
    else 
    {
      tmp = second_byte;
      second_byte = 0;
    }    

    UDR0 = ~tmp;  // put byte to send buffer
    
    tmp = 0; // this is important!
  }
}

///
/// Prepare sector header template
///////////////////////////////////////////
void prepare_sector_header()
{
  // Address field
  byte i;
  for(i=0; i <= 11; i++) sector_header[i] = 0x00; // 0x00(0)-0x0B(11) sync field
  for(i=12; i <= 14; i++) sector_header[i] = 0xA1; // 0x0C(12)-0x0E(14) 3x0xA1
  sector_header[15] = 0xFE;         // 0x0F(15) 0xFE
  // TRACK, SIDE, SECTOR
  sector_header[16] = 0x00;         // 0x10(16) track
  sector_header[17] = 0x00;         // 0x11(17) side
  sector_header[18] = 0x01;         // 0x12(18) sector
  sector_header[19] = 0x01;         // 0x13(19) sector len (256 bytes)
  sector_header[20] = 0xFA;         // 0x14(20) CRC1 for trk=0, side=0, sector=1
  sector_header[21] = 0x0C;         // 0x15(21) CRC2
  // GAP 2
  for(i=22; i <= 43; i++) sector_header[i] = 0x4E; // 0x16(22)-0x2B
  // DATA field
  for(i=44; i <= 55; i++) sector_header[i] = 0x00; // 0x2C(44)-0x37 sync field
  for(i=56; i <= 58; i++) sector_header[i] = 0xA1; // 0x38(56)-0x3A
  sector_header[59] = 0xFB;         // 0x3B(59)
}

///
/// Prepare track header
///////////////////////////////////////////
void prepare_track_header()
{
  // Address field
  byte i;
  for(i=0; i < 12; i++) track_header[i] = 0x00;
  sector_header[12] = 0xC2;
  sector_header[13] = 0xC2;
  sector_header[14] = 0xC2;
  sector_header[15] = 0xFC;
  for(i=16; i < 66; i++) track_header[i] = 0x4E;
}

///
/// Interrupts enable/disable functions
///////////////////////////////////////////
void USART_enable() { UCSR0B |= 0x20; }
void USART_disable() { UCSR0B &= ~0x20; }
void INT0_enable() { EIMSK |= 0x01; }
void INT0_disable() { EIMSK &= ~0x01; }

/// FDD Emulator initialization
///////////////////////////////////////////
void emu_init()
{
  cli(); // DISABLE GLOBAL INTERRUPTS
  
  prepare_track_header();
  prepare_sector_header();

  // Setup USART in MasterSPI mode 500000bps
  UBRR0H = 0x00;
  UBRR0L = 0x0F; // 500 kbps
  UCSR0C = 0xC0;
  UCSR0A = 0x00;
  UCSR0B = 0x08; // disabled

  //INIT INT0 interrupt
  EICRA = 0x03; // falling edge=2, rising edge=3

  sei();   // ENABLE GLOBAL INTERRUPTS

  // INIT pins and ports
  PORTC |= _BV(DRIVE_SEL) | _BV(SIDE_SEL); // set pull_up
  //SET INDEX,TRK00 AS OUTPUT AND HIGH, WP AS OUTPUT AND LOW
  PORTD |= _BV(MOTOR_ON) | _BV(INDEX) | _BV(TRK00); // set 1 output
  PORTD |= _BV(STEP) | _BV(WRT_GATE); // set pull-up
  PORTD &= ~_BV(WP); // set 0
  PORTB |= _BV(DIR_SEL);
  DDRD |= _BV(INDEX) | _BV(TRK00) | _BV(WP);
  // Init SPI for SD Card
  SPI_DDR = _BV(SPI_MOSI) | _BV(SPI_SCK) | _BV(SPI_CS); //set output mode for MOSI, SCK ! move SS to GND
  SPCR = _BV(MSTR) | _BV(SPE);   // Master mode, SPI enable, clock speed MCU_XTAL/4, LSB first
  SPSR = _BV(SPI2X);             // double speed
}


///
/// MAIN Routine
///////////////////////////////////////////
int main() {
  init(); // init arduino libraries

  emu_init(); // initialize FDD emulator

  while(1)
  { // MAIN LOOP START
      /// MAIN LOOP USED FOR SELECT and INIT SD CARD and other
      
      //>>>>>> print "NO CARD PRESENT"
      while(pf_mount(&fat) != FR_OK);
      //>>>>>> print "CARD INFO, TRD IMAGE NAME"
  
      while (1)
      { /// DRIVE SELECT LOOP
        //loop_until_bit_is_set(PINC,MOTOR_ON); // wait motor on
        while (PIND & _BV(MOTOR_ON));
    
        /////////////////////////////////////////////////////////////////
        // INIT after drive selected
        side = s_side = tmp = state = b_index = data_sent = cylinder = s_cylinder = 0;    
        sector = 1;
    
        while(pf_open("default.trd") != FR_OK);
        
        chain_size = fat.csize / 8;

        max_cylinder = (fat.fsize/4096 <= MAX_CYL)?fat.fsize/4096:MAX_CYL; // calculate maximal cylinder

        // create cluster table for tracks
        for(int i = 0; i < MAX_TRACKS; i++)
        {
            if( pf_lseek(fat.fptr + 4096) != FR_OK ) break;
            clust_table[i]=fat.curr_clust;
        }
      
        curr_clust = clust_table[0];
        dsect = fat.database + (curr_clust - 2) * fat.csize;
        chain_index = 0;
        card_read_sector(sector_data,fat.dsect);
        fptr_sect_main = 0; // file start sector
        fptr_sect = 1; // file current sector
        fat.curr_clust = curr_clust;
        fat.dsect = dsect + 1;
        /////////////////////////////////////////////////////////////////

        // ENABLE INDERRUPTS (USART and STEP pin)
        USART_enable();
        INT0_enable();

        do { // READ DATA SEND LOOP (send data from FDD to FDD controller)  
        //-------------------------------------------------------------------------------------------
            //>>>>>> print "CYLINDER, HEAD INFO"

            while (!data_sent); // wait until sector data not sent
            
            if( sector == 1 )
            {   
                // Current SIDE detect
                // code same as "if(PINC & 1) side = 0; else side = 1;"
                ///////////////////////////////////////////////////////
                asm("clr r5");
                asm("sbis 0x06,0");
                asm("inc r5");
                ///////////////////////////////////////////////////////
          
                if (s_cylinder != cylinder || s_side != side)
                { // if track or side changed we need to change offset in TRD file
                    s_cylinder = cylinder; // current Floppy cylinder
                    s_side = side; // current Floppy side
                    uint8_t tr_offset = s_cylinder * 2 + s_side; // track number
                    fptr_sect_main = tr_offset * 8; // track offset in TRD file in sd_card sectors
                    curr_clust = clust_table[tr_offset]; // track first cluster in FAT32
                    dsect = fat.database + (curr_clust - 2) * fat.csize + (fptr_sect_main & (fat.csize-1)); // track start LBA number on SD card
                }
                chain_index = 0;
                chain_gen_index = 0;
                // update values for current track
                fptr_sect = fptr_sect_main;
                fat.curr_clust = curr_clust;
                fat.dsect = dsect;
            }
            if(sector & 1)
            {
                if( sector > 1 && !(fptr_sect & (fat.csize - 1)) )
                { // on cluster boundary, get next cluster number and calculate LBA
                    fat.curr_clust = cluster_chain[chain_index++];
                    fat.dsect = fat.database + (fat.curr_clust - 2) * fat.csize;
                }
                card_read_sector(sector_data,fat.dsect++); // read 2 floppy sectors from SD card (1 SD card sector) and increase LBA
                fptr_sect++; // increase SD card sector in FAT32 cluster;
            }
            else if(chain_size >= sector/2)
            { // create cluster chain table when not reading sectors
                    cluster_chain[chain_gen_index] = (chain_gen_index == 0) ? get_fat(curr_clust) : get_fat(cluster_chain[chain_gen_index-1]);
                    chain_gen_index++;
            }

            data_sent = 0;
        
        } while (!(PIND & _BV(MOTOR_ON))); // READ DATA SEND LOOP END
        //-------------------------------------------------------------------------------------------
    
        // DISABLE INDERRUPTS (USART and STEP pin)
        USART_disable();
        INT0_disable();

        PORTD |= _BV(TRK00); // Set TRACK 00 HIGH
      } /// DRIVE SELECT LOOP END
      
  } // MAIN LOOP END
  
} // END MAIN