#ifndef MCP23S17_H
#define MCP23S17_H

#include "main.h"

// MCP23S17 Registers (IOCON.BANK = 0)
#define MCP_IODIRA   0x00
#define MCP_IODIRB   0x01
#define MCP_IPOLA    0x02
#define MCP_IPOLB    0x03
#define MCP_GPINTENA 0x04
#define MCP_GPINTENB 0x05
#define MCP_DEFVALA  0x06
#define MCP_DEFVALB  0x07
#define MCP_INTCONA  0x08
#define MCP_INTCONB  0x09
#define MCP_IOCON    0x0A
#define MCP_GPPUA    0x0C
#define MCP_GPPUB    0x0D
#define MCP_INTFA    0x0E
#define MCP_INTFB    0x0F
#define MCP_INTCAPA  0x10
#define MCP_INTCAPB  0x11
#define MCP_GPIOA    0x12
#define MCP_GPIOB    0x13
#define MCP_OLATA    0x14
#define MCP_OLATB    0x15

// Opcode (Address A2=0, A1=0, A0=0)
#define MCP_OPCODE_WRITE 0x40
#define MCP_OPCODE_READ  0x41

void MCP_Init(void);
void MCP_Write(uint8_t reg, uint8_t value);
uint8_t MCP_Read(uint8_t reg);
uint8_t MCP_IsReady(void);
uint8_t MCP_TestLink(void);

#endif
