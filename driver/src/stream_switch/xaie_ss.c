/******************************************************************************
* Copyright (C) 2019 - 2020 Xilinx, Inc.  All rights reserved.
* SPDX-License-Identifier: MIT
******************************************************************************/


/*****************************************************************************/
/**
* @file xaie_ss.c
* @{
*
* This file contains routines for AIE stream switch
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who     Date     Changes
* ----- ------  -------- -----------------------------------------------------
* 1.0   Tejus   09/24/2019  Initial creation
* 1.1   Tejus   10/21/2019  Optimize stream switch data structures
* 1.2   Tejus	01/04/2020  Cleanup error messages
* 1.3   Tejus   03/20/2020  Make internal function static
* 1.4   Tejus   03/21/2020  Fix slave port configuration bug
* 1.5   Tejus   03/21/2020  Add stream switch packet switch mode apis
* 1.6   Tejus   04/13/2020  Remove range apis and change to single tile apis
* </pre>
*
******************************************************************************/
/***************************** Include Files *********************************/
#include "xaie_ss.h"

/************************** Constant Definitions *****************************/
#define XAIE_SS_MASTER_PORT_ARBITOR_LSB		0U
#define XAIE_SS_MASTER_PORT_ARBITOR_MASK	0x7
#define XAIE_SS_MASTER_PORT_MSELEN_LSB		0x3
#define XAIE_SS_MASTER_PORT_MSELEN_MASK		0x78

#define XAIE_SS_ARBITOR_MAX			0x7
#define XAIE_SS_MSEL_MAX			0x3
#define XAIE_SS_MASK				0x1F
#define XAIE_SS_MSELEN_MAX			0xF

#define XAIE_PACKETID_MAX			0x1F
/************************** Function Definitions *****************************/
/*****************************************************************************/
/**
*
* To configure stream switch master registers, slave index has to be calculated
* from the internal data structure. The routine calculates the slave index for
* any tile type.
*
* @param	StrmMod: Stream Module pointer
* @param	Slave: Stream switch port type
* @param	PortNum: Slave port number
* @param	SlaveIdx: Place holder for the routine to store the slave idx
*
* @return	XAIE_OK on success and XAIE_INVALID_RANGE on failure
*
* @note		Internal API only.
*
******************************************************************************/
static AieRC _XAie_GetSlaveIdx(const XAie_StrmMod *StrmMod, StrmSwPortType Slave,
		u8 PortNum, u8 *SlaveIdx)
{
	u32 BaseAddr;
	u32 RegAddr;
	const XAie_StrmPort *PortPtr;

	/* Get Base Addr of the slave tile from Stream Switch Module */
	BaseAddr = StrmMod->SlvConfigBaseAddr;

	PortPtr = &StrmMod->SlvConfig[Slave];

	/* Return error if the Slave Port Type is not valid */
	if((PortPtr->NumPorts == 0) || (PortNum >= PortPtr->NumPorts)) {
		XAieLib_print("Error: Invalid Slave Port\n");
		return XAIE_ERR_STREAM_PORT;
	}

	RegAddr = PortPtr->PortBaseAddr + StrmMod->PortOffset * PortNum;
	*SlaveIdx = (RegAddr - BaseAddr) / 4;

	return XAIE_OK;
}

/*****************************************************************************/
/**
*
* This API is used to get the register offset and value required to configure
* the selected slave port of the stream switch in the corresponding tile.
*
* @param	PortPtr - Pointer to the internal port data structure.
* @param	PortNum - Port Number.
* @param	Enable - Enable/Disable the slave port (1-Enable,0-Disable).
* @param	PktEnable - Enable/Disable the packet switching mode
*		(1-Enable,0-Disable).
* @param	RegVal - pointer to store the register value.
* @param	RegOff - pointer to store the regster offset.
*
* @return	XAIE_OK on success and error code on failure.
*
* @note		Internal API.
*
*******************************************************************************/
static AieRC _XAie_StrmConfigSlv(const XAie_StrmMod *StrmMod,
		StrmSwPortType PortType, u8 PortNum, u8 Enable, u8 PktEnable,
		u32 *RegVal, u32 *RegOff)
{
	*RegVal = 0U;
	const XAie_StrmPort  *PortPtr;

	/* Get the slave port pointer from stream module */
	PortPtr = &StrmMod->SlvConfig[PortType];

	if((PortPtr->NumPorts == 0) || (PortNum >= PortPtr->NumPorts)) {
		XAieLib_print("Error: Invalid Slave Port\n");
		return XAIE_ERR_STREAM_PORT;
	}

	*RegOff = PortPtr->PortBaseAddr + StrmMod->PortOffset * PortNum;

	if(Enable != XAIE_ENABLE)
		return XAIE_OK;

	/* Frame the 32-bit reg value */
	*RegVal = XAie_SetField(Enable, StrmMod->SlvEn.Lsb,
			StrmMod->SlvEn.Mask) |
		XAie_SetField(PktEnable,
				StrmMod->SlvPktEn.Lsb, StrmMod->SlvPktEn.Mask);

	return XAIE_OK;
}

/*****************************************************************************/
/**
*
* This API is used to get the register offset and value required to configure
* the selected master port of the stream switch in the corresponding tile.
*
* @param	PortPtr - Pointer to the internal port data structure.
* @param	PortNum - Port Number.
* @param	Enable - Enable/Disable the slave port (1-Enable,0-Disable).
* @param	PktEnable - Enable/Disable the packet switching mode
*		(1-Enable,0-Disable).
* @param	RegVal - pointer to store the register value.
* @param	RegOff - pointer to store the regster offset.
*
* @return	XAIE_OK on success and error code on failure.
*
* @note		Internal API.
*
*******************************************************************************/
static AieRC _StrmConfigMstr(const XAie_StrmMod *StrmMod,
		StrmSwPortType PortType, u8 PortNum, u8 Enable, u8 PktEnable,
		u8 Config, u32 *RegVal, u32 *RegOff)
{

	u8 DropHdr;
	*RegVal = 0U;
	const XAie_StrmPort *PortPtr;

	PortPtr = &StrmMod->MstrConfig[PortType];

	if((PortPtr->NumPorts == 0) || (PortNum >= PortPtr->NumPorts)) {
		XAieLib_print("Error: Invalid Stream Port\n");
		return XAIE_ERR_STREAM_PORT;
	}

	*RegOff = PortPtr->PortBaseAddr + StrmMod->PortOffset * PortNum;
	if(Enable != XAIE_ENABLE)
		return XAIE_OK;

	/* Extract the drop header field */
	DropHdr = XAie_GetField(Config, StrmMod->DrpHdr.Lsb,
			StrmMod->DrpHdr.Mask);

	/* Frame 32-bit reg value */
	*RegVal = XAie_SetField(Enable, StrmMod->MstrEn.Lsb,
			StrmMod->MstrEn.Mask) |
		XAie_SetField(PktEnable, StrmMod->MstrPktEn.Lsb,
				StrmMod->MstrPktEn.Mask) |
		XAie_SetField(DropHdr, StrmMod->DrpHdr.Lsb,
				StrmMod->DrpHdr.Mask) |
		XAie_SetField(Config, StrmMod->Config.Lsb,
				StrmMod->Config.Mask);

	return XAIE_OK;
}

/*****************************************************************************/
/**
*
* This API is used to connect the selected master port to the specified slave
* port of the stream switch switch in ciruit switch mode.
*
* @param	DevInst: Device Instance
* @param	Loc: Loc of AIE Tiles
* @param	Slave - Slave port type.
* @param	SlvPortNum- Slave port number.
* @param	Master - Master port type.
* @param	MstrPortNum- Master port number.
* @param	SlvEnable - Enable/Disable the slave port (1-Enable,0-Disable).
*
* @return	XAIE_OK on success, Error code on failure.
*
* @note		Internal only.
*
*******************************************************************************/
static AieRC _XAie_StreamSwitchConfigureCct(XAie_DevInst *DevInst,
		XAie_LocType Loc, StrmSwPortType Slave, u8 SlvPortNum,
		StrmSwPortType Master, u8 MstrPortNum, u8 Enable)
{
	AieRC RC;
	u64 MstrAddr;
	u64 SlvAddr;
	u32 MstrOff;
	u32 MstrVal;
	u32 SlvOff;
	u32 SlvVal;
	u8 SlaveIdx;
	u8 TileType;
	const XAie_StrmMod *StrmMod;

	if((DevInst == XAIE_NULL) ||
			(DevInst->IsReady != XAIE_COMPONENT_IS_READY)) {
		XAieLib_print("Error: Invalid Device Instance\n");
		return XAIE_INVALID_ARGS;
	}

	if((Slave >= SS_PORT_TYPE_MAX) || (Master >= SS_PORT_TYPE_MAX)) {
		XAieLib_print("Error: Invalid Stream Switch Ports\n");
		return XAIE_ERR_STREAM_PORT;
	}

	TileType = _XAie_GetTileTypefromLoc(DevInst, Loc);
	if(TileType == XAIEGBL_TILE_TYPE_MAX) {
		XAieLib_print("Error: Invalid Tile Type\n");
		return XAIE_INVALID_TILE;
	}

	/* Get stream switch module pointer from device instance */
	StrmMod = DevInst->DevProp.DevMod[TileType].StrmSw;

	RC = _XAie_GetSlaveIdx(StrmMod, Slave, SlvPortNum, &SlaveIdx);
	if(RC != XAIE_OK) {
		XAieLib_print("Error: Unable to compute Slave Index\n");
		return RC;
	}

	/* Compute the register value and register address for the master port*/
	RC = _StrmConfigMstr(StrmMod, Master, MstrPortNum, Enable, XAIE_DISABLE,
			SlaveIdx, &MstrVal, &MstrOff);
	if(RC != XAIE_OK) {
		XAieLib_print("Error: Master config error\n");
		return RC;
	}

	/* Compute the register value and register address for slave port */
	RC = _XAie_StrmConfigSlv(StrmMod, Slave, SlvPortNum, Enable,
			XAIE_DISABLE, &SlvVal, &SlvOff);
	if(RC != XAIE_OK) {
		XAieLib_print("Error: Slave config error\n");
		return RC;
	}

	/* Compute absolute address and write to register */
	MstrAddr = DevInst->BaseAddr + MstrOff +
		_XAie_GetTileAddr(DevInst, Loc.Row, Loc.Col);
	SlvAddr = DevInst->BaseAddr + SlvOff +
		_XAie_GetTileAddr(DevInst, Loc.Row, Loc.Col);

	XAieGbl_Write32(MstrAddr, MstrVal);
	XAieGbl_Write32(SlvAddr, SlvVal);

	return XAIE_OK;
}

/*****************************************************************************/
/**
*
* This API is used to enable the connection between the selected master port
* to the specified slave port of the stream switch switch in ciruit switch mode.
*
* @param	DevInst: Device Instance
* @param	Loc: Loc of AIE Tiles
* @param	Slave: Slave port type.
* @param	SlvPortNum: Slave port number.
* @param	Master: Master port type.
* @param	MstrPortNum: Master port number.
*
* @return	XAIE_OK on success, Error code on failure.
*
* @note		None.
*
*******************************************************************************/
AieRC XAie_StrmConnCctEnable(XAie_DevInst *DevInst, XAie_LocType Loc,
		StrmSwPortType Slave, u8 SlvPortNum, StrmSwPortType Master,
		u8 MstrPortNum)
{
	return _XAie_StreamSwitchConfigureCct(DevInst, Loc, Slave, SlvPortNum,
			Master, MstrPortNum, XAIE_ENABLE);

}

/*****************************************************************************/
/**
*
* This API is used to disable the connection between the selected master port
* to the specified slave port of the stream switch in ciruit switch mode.
*
* @param	DevInst: Device Instance
* @param	Loc: Loc of AIE Tiles
* @param	Slave: Slave port type.
* @param	SlvPortNum: Slave port number.
* @param	Master: Master port type.
* @param	MstrPortNum: Master port number.
*
* @return	XAIE_OK on success, Error code on failure.
*
* @note		None.
*
*******************************************************************************/
AieRC XAie_StrmConnCctDisable(XAie_DevInst *DevInst, XAie_LocType Loc,
		StrmSwPortType Slave, u8 SlvPortNum, StrmSwPortType Master,
		u8 MstrPortNum)
{
	return _XAie_StreamSwitchConfigureCct(DevInst, Loc, Slave, SlvPortNum,
			Master, MstrPortNum, XAIE_DISABLE);

}

/*****************************************************************************/
/**
*
* This API is used to configure the slave port of a stream switch.
*
* @param	DevInst: Device Instance
* @param	Loc: Loc of AIE Tiles
* @param	Slave: Slave port type.
* @param	SlvPortNum: Slave port number.
* @param	PktEn: XAIE_ENABLE/XAIE_DISABLE to Enable/Disable packet switch
*		mode.
* @param	Enable: XAIE_ENABLE/XAIE_DISABLE to Enable/Disable slave port.
*
* @return	XAIE_OK on success, Error code on failure.
*
* @note		Internal only.
*
*******************************************************************************/
static AieRC _XAie_StrmSlavePortConfig(XAie_DevInst *DevInst, XAie_LocType Loc,
		StrmSwPortType Slave, u8 SlvPortNum, u8 EnPkt, u8 Enable)
{
	AieRC RC;
	u64 Addr;
	u32 RegOff;
	u32 RegVal = 0U;
	u8 TileType;
	const XAie_StrmMod *StrmMod;

	if((DevInst == XAIE_NULL) ||
			(DevInst->IsReady != XAIE_COMPONENT_IS_READY)) {
		XAieLib_print("Error: Invalid Device Instance\n");
		return XAIE_INVALID_ARGS;
	}

	if((Slave >= SS_PORT_TYPE_MAX)) {
		XAieLib_print("Error: Invalid Stream Switch Ports\n");
		return XAIE_ERR_STREAM_PORT;
	}

	TileType = _XAie_GetTileTypefromLoc(DevInst, Loc);
	if(TileType == XAIEGBL_TILE_TYPE_MAX) {
		XAieLib_print("Error: Invalid Tile Type\n");
		return XAIE_INVALID_TILE;
	}

	/* Get stream switch module pointer from device instance */
	StrmMod = DevInst->DevProp.DevMod[TileType].StrmSw;

	/* Compute the register value and register address for slave port */
	RC = _XAie_StrmConfigSlv(StrmMod, Slave, SlvPortNum, EnPkt,
			Enable, &RegVal, &RegOff);
	if(RC != XAIE_OK) {
		XAieLib_print("Error: Slave config error\n");
		return RC;
	}

	Addr = DevInst->BaseAddr +
		_XAie_GetTileAddr(DevInst, Loc.Row, Loc.Col) + RegOff;

	XAieGbl_Write32(Addr, RegVal);

	return XAIE_OK;
}

/*****************************************************************************/
/**
*
* This API is used to Enable the slave port of a stream switch in packet switch
* mode.
*
* @param	DevInst: Device Instance
* @param	Loc: Loc of AIE Tiles
* @param	Slave: Slave port type.
* @param	SlvPortNum: Slave port number.
*
* @return	XAIE_OK on success, Error code on failure.
*
* @note		None.
*
*******************************************************************************/
AieRC XAie_StrmPktSwSlavePortEnable(XAie_DevInst *DevInst, XAie_LocType Loc,
		StrmSwPortType Slave, u8 SlvPortNum)
{
	return _XAie_StrmSlavePortConfig(DevInst, Loc, Slave, SlvPortNum,
			XAIE_ENABLE, XAIE_ENABLE);
}

/*****************************************************************************/
/**
*
* This API is used to Disable the slave port of a stream switch in packet switch
* mode.
*
* @param	DevInst: Device Instance
* @param	Loc: Loc of AIE Tiles
* @param	Slave: Slave port type.
* @param	SlvPortNum: Slave port number.
*
* @return	XAIE_OK on success, Error code on failure.
*
* @note		None.
*
*******************************************************************************/
AieRC XAie_StrmPktSwSlavePortDisable(XAie_DevInst *DevInst, XAie_LocType Loc,
		StrmSwPortType Slave, u8 SlvPortNum)
{
	return _XAie_StrmSlavePortConfig(DevInst, Loc, Slave, SlvPortNum,
			XAIE_DISABLE, XAIE_DISABLE);
}

/*****************************************************************************/
/**
*
* This API is used to configure the register fields of Master ports for
* configuration in packet switch mode.
*
* @param	DevInst: Device Instance
* @param	Loc: Loc of AIE Tiles
* @param	Master: Master port type.
* @param	MstrPortNum: Master port number.
* @param	DropHeader: Enable or disable the drop header bit
* @param	Arbitor: Arbitor to use for this packet switch connection
* @param	MselEn: MselEn field in the Master port register field
* @param	PktEn: XAIE_ENABLE/XAIE_DISABLE to Enable/Disable packet switch
*		mode.
* @param	Enable: XAIE_ENABLE/XAIE_DISABLE to Enable/Disable master port.
*
* @return	XAIE_OK on success, Error code on failure.
*
* @note		Internal only. When Enable is XAIE_DISABLE, the API configures
*		Master port register to reset value.
*
*
*******************************************************************************/
static AieRC _XAie_StrmPktSwMstrPortConfig(XAie_DevInst *DevInst,
		XAie_LocType Loc, StrmSwPortType Master, u8 MstrPortNum,
		XAie_StrmSwPktHeader DropHeader, u8 Arbitor, u8 MSelEn,
		u8 PktEn, u8 Enable)
{
	AieRC RC;
	u64 Addr;
	u32 RegOff;
	u32 RegVal;
	u8 TileType;
	const XAie_StrmMod *StrmMod;
	u32 Config = 0U;

	if((DevInst == XAIE_NULL) ||
			(DevInst->IsReady != XAIE_COMPONENT_IS_READY)) {
		XAieLib_print("Error: Invalid Device Instance\n");
		return XAIE_INVALID_ARGS;
	}

	if((Arbitor > XAIE_SS_ARBITOR_MAX) || (MSelEn > XAIE_SS_MSELEN_MAX)) {
		XAieLib_print("Error: Invalid Arbitor or MSel Enable\n");
		return XAIE_INVALID_ARGS;
	}

	if((Master >= SS_PORT_TYPE_MAX)) {
		XAieLib_print("Error: Invalid Stream Switch Ports\n");
		return XAIE_ERR_STREAM_PORT;
	}

	TileType = _XAie_GetTileTypefromLoc(DevInst, Loc);
	if(TileType == XAIEGBL_TILE_TYPE_MAX) {
		XAieLib_print("Error: Invalid Tile Type\n");
		return XAIE_INVALID_TILE;
	}

	/* Get stream switch module pointer from device instance */
	StrmMod = DevInst->DevProp.DevMod[TileType].StrmSw;

	/* Construct Config and Drop header register fields */
	if(Enable == XAIE_ENABLE) {
		Config = XAie_SetField(DropHeader, StrmMod->DrpHdr.Lsb,
				StrmMod->DrpHdr.Mask) |
			XAie_SetField(Arbitor, XAIE_SS_MASTER_PORT_ARBITOR_LSB,
					XAIE_SS_MASTER_PORT_ARBITOR_MASK) |
			XAie_SetField(MSelEn, XAIE_SS_MASTER_PORT_MSELEN_LSB,
					XAIE_SS_MASTER_PORT_MSELEN_MASK);
	}

	/* Compute the register value and register address for the master port*/
	RC = _StrmConfigMstr(StrmMod, Master, MstrPortNum, Enable, PktEn,
			Config, &RegVal, &RegOff);
	if(RC != XAIE_OK) {
		XAieLib_print("Error: Master config error\n");
		return RC;
	}

	Addr = DevInst->BaseAddr +
		_XAie_GetTileAddr(DevInst, Loc.Row, Loc.Col) + RegOff;

	XAieGbl_Write32(Addr, RegVal);

	return XAIE_OK;
}

/*****************************************************************************/
/**
*
* This API is used to Enable the Master ports with configuration for packet
* switch mode.
*
* @param	DevInst: Device Instance
* @param	Loc: Loc of AIE Tiles
* @param	Master: Master port type.
* @param	MstrPortNum: Master port number.
* @param	DropHeader: Enable or disable the drop header bit
* @param	Arbitor: Arbitor to use for this packet switch connection
* @param	MSelEn: MselEn field in the Master port register field
*
* @return	XAIE_OK on success, Error code on failure.
*
* @note		None.
*
*
*******************************************************************************/
AieRC XAie_StrmPktSwMstrPortEnable(XAie_DevInst *DevInst, XAie_LocType Loc,
		StrmSwPortType Master, u8 MstrPortNum,
		XAie_StrmSwPktHeader DropHeader, u8 Arbitor, u8 MSelEn)
{
	return _XAie_StrmPktSwMstrPortConfig(DevInst, Loc, Master, MstrPortNum,
			DropHeader, Arbitor, MSelEn, XAIE_ENABLE, XAIE_ENABLE);
}

/*****************************************************************************/
/**
*
* This API is used to configure the register fields of Master ports for
* configuration in packet switch mode.
*
* @param	DevInst: Device Instance
* @param	Loc: Loc of AIE Tiles
* @param	Master: Master port type.
* @param	MstrPortNum: Master port number.
*
* @return	XAIE_OK on success, Error code on failure.
*
* @note		Configures Master port register to reset value.
*
*
*******************************************************************************/
AieRC XAie_StrmPktSwMstrPortDisable(XAie_DevInst *DevInst, XAie_LocType Loc,
		StrmSwPortType Master, u8 MstrPortNum)
{
	return _XAie_StrmPktSwMstrPortConfig(DevInst, Loc, Master, MstrPortNum,
			XAIE_SS_PKT_DONOT_DROP_HEADER, 0U, 0U, XAIE_DISABLE,
			XAIE_DISABLE);
}

/*****************************************************************************/
/**
*
* This API is used to configure the stream switch slave slot configuration
* registers. This API should be used in combination with other APIs to
* first configure the master and slave ports in packet switch mode. Disabling
* the slave port slot writes reset values to the registers.
*
* @param	DevInst: Device Instance
* @param	Loc: Loc of AIE Tiles
* @param	Slave: Slave port type
* @param	SlvPortNum: Slave port number
* @param	SlotNum: Slot number for the slave port
* @param	Pkt: Packet with initialized packet id and packet type
* @param	Mask: Mask field in the slot register
* @param	Msel: Msel register field in the slave slot register
* @param	Arbitor: Arbitor to use for this packet switch connection
* @param	Enable: XAIE_ENABLE/XAIE_DISABLE to Enable or disable
*
* @return	XAIE_OK on success, Error code on failure.
*
* @note		Internal Only.
*
*******************************************************************************/
static AieRC _XAie_StrmSlaveSlotConfig(XAie_DevInst *DevInst, XAie_LocType Loc,
		StrmSwPortType Slave, u8 SlvPortNum, u8 SlotNum,
		XAie_Packet Pkt, u8 Mask, u8 MSel, u8 Arbitor, u8 Enable)
{
	u8 TileType;
	u64 RegAddr;
	u32 RegVal = 0U;
	const XAie_StrmMod *StrmMod;

	if((DevInst == XAIE_NULL) ||
			(DevInst->IsReady != XAIE_COMPONENT_IS_READY)) {
		XAieLib_print("Error: Invalid Device Instance\n");
		return XAIE_INVALID_ARGS;
	}

	if((Arbitor > XAIE_SS_ARBITOR_MAX) || (MSel > XAIE_SS_MSEL_MAX) ||
			(Mask & ~XAIE_SS_MASK) ||
			(Pkt.PktId > XAIE_PACKETID_MAX)) {
		XAieLib_print("Error: Invalid Arbitor, MSel, PktId or Mask\n");
		return XAIE_INVALID_ARGS;
	}

	TileType = _XAie_GetTileTypefromLoc(DevInst, Loc);
	if(TileType == XAIEGBL_TILE_TYPE_MAX) {
		XAieLib_print("Error: Invalid Tile Type\n");
		return XAIE_INVALID_TILE;
	}

	/* Get stream switch module pointer from device instance */
	StrmMod = DevInst->DevProp.DevMod[TileType].StrmSw;
	if((Slave >= SS_PORT_TYPE_MAX) || (SlotNum >= StrmMod->NumSlaveSlots) ||
			(SlvPortNum >= StrmMod->SlvConfig[Slave].NumPorts)) {
		XAieLib_print("Error: Invalid Slave port and slot arguments\n");
		return XAIE_ERR_STREAM_PORT;
	}

	RegAddr = DevInst->BaseAddr +
		_XAie_GetTileAddr(DevInst, Loc.Row, Loc.Col) +
		StrmMod->SlvSlotConfig[Slave].PortBaseAddr +
		SlvPortNum * StrmMod->SlotOffsetPerPort +
		SlotNum * StrmMod->SlotOffset;

	if(Enable == XAIE_ENABLE) {
		RegVal = XAie_SetField(Pkt.PktId, StrmMod->SlotPktId.Lsb,
				StrmMod->SlotPktId.Mask) |
			XAie_SetField(Mask, StrmMod->SlotMask.Lsb,
					StrmMod->SlotMask.Mask) |
			XAie_SetField(XAIE_ENABLE, StrmMod->SlotEn.Lsb,
					StrmMod->SlotEn.Mask) |
			XAie_SetField(MSel, StrmMod->SlotMsel.Lsb,
					StrmMod->SlotMsel.Mask) |
			XAie_SetField(Arbitor, StrmMod->SlotArbitor.Lsb,
					StrmMod->SlotArbitor.Mask);
	}

	XAieGbl_Write32(RegAddr, RegVal);

	return XAIE_OK;
}

/*****************************************************************************/
/**
*
* This API is used to configure the stream switch slave slot configuration
* registers. This API should be used in combination with other APIs to
* first configure the master and slave ports in packet switch mode.
*
* @param	DevInst: Device Instance
* @param	Loc: Loc of AIE Tiles
* @param	Slave: Slave port type
* @param	SlvPortNum: Slave port number
* @param	SlotNum: Slot number for the slave port
* @param	Pkt: Packet with initialized packet id and packet type
* @param	Mask: Mask field in the slot register
* @param	MSel: Msel register field in the slave slot register
* @param	Arbitor: Arbitor to use for this packet switch connection
*
* @return	XAIE_OK on success, Error code on failure.
*
* @note		None.
*
*******************************************************************************/
AieRC XAie_StrmPktSwSlaveSlotEnable(XAie_DevInst *DevInst, XAie_LocType Loc,
		StrmSwPortType Slave, u8 SlvPortNum, u8 SlotNum,
		XAie_Packet Pkt, u8 Mask, u8 MSel, u8 Arbitor)
{
	return _XAie_StrmSlaveSlotConfig(DevInst, Loc, Slave, SlvPortNum,
			SlotNum, Pkt, Mask, MSel, Arbitor, XAIE_ENABLE);
}

/*****************************************************************************/
/**
*
* This API is used to disable the stream switch slave port slots. The API
* disables the slot and writes reset values to all other fields.
*
* @param	DevInst: Device Instance
* @param	Loc: Loc of AIE Tiles
* @param	Slave: Slave port type
* @param	SlvPortNum: Slave port number
* @param	SlotNum: Slot number for the slave port
*
* @return	XAIE_OK on success, Error code on failure.
*
* @note		None.
*
*******************************************************************************/
AieRC XAie_StrmPktSwSlaveSlotDisable(XAie_DevInst *DevInst, XAie_LocType Loc,
		StrmSwPortType Slave, u8 SlvPortNum, u8 SlotNum)
{
	XAie_Packet Pkt = XAie_PacketInit(0U, 0U);
	return _XAie_StrmSlaveSlotConfig(DevInst, Loc, Slave, SlvPortNum,
			SlotNum, Pkt, 0U, 0U, 0U, XAIE_DISABLE);
}

/** @} */
