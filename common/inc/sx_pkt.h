#if !defined(PI_PORTAL_PKT)
#define PI_PORTAL_PKT

#include "sx_types.h"

// TODO
#define PIRACAST_PKT_HDR_SIZE			(sizeof(unsigned int))
#define PIRACAST_PKT_PAYLOAD_SIZE_MAX		(1472-PIRACAST_PKT_HDR_SIZE)

/* -------------------------------------------------
/       HDR_GET_FUNCT 
/		MPEG2 Transport Stream header GET specific content
/--------------------------------------------------*/
#define PID_GET(hdr)		((((hdr).tei_pusi_tp_pid1 & 0x1F) << 8) | (hdr).pid2)
#define CC_GET(hdr)		((hdr).tsc_aff_pf_cc & 0x0F)
#define PUSI_GET(hdr)		(((hdr).tei_pusi_tp_pid1 & 0x40) >> 6)
#define AFF_PF_GET(hdr)		(((hdr).tsc_aff_pf_cc & 0x30) >> 4)



/* -------------------------------------------------
/       sRTP_HDR 
/		RTP header
/		0 1 2 3 4 5 6 7 8      9-15        16-31
/		+--+--+-+-------+-+-----------+---------------+
/		| V|P|X|   CC  |M|Payload type|Sequence number|
/		+----------------------------------------------
/		|		Timestamp		      |
/		+---------------------------------------------+
/		|		SSRC identifier		      |
/		+---------------------------------------------+	
/---------------------------------------------------*/
typedef struct{
	UINT8		version_p_x_cc;
	UINT8		m_pt;
	UINT16		sequence_num;
	UINT32		timestamp;
	UINT32		ssrc_id;

} sRTP_HDR;	



/* -------------------------------------------------
/       sMPEG2_TS_HDR 
/		MPEG2 Transport Stream header
/--------------------------------------------------*/
typedef struct{
	UINT8		sync_byte;		///< Sync byte = 0x47(ASCCI 'G')   ,8 bits
	UINT8		tei_pusi_tp_pid1;	///< Trasnport Error Indicator	   ,1 bit
						///< Payload Uint Start Indicator  ,1 bit
						///< Transport Pirority		   ,1 bit
						///< PID first half		   ,5 bits
	UINT8		pid2;			///< PID second half		   ,8 bits
	UINT8		tsc_aff_pf_cc;		///< Transport Scrambling Control  ,2 bits
						///< Adaption Field Flag	   ,1 bit
						///< Payload Flag		   ,1 bit
						///< Continuity Counter		   ,4 bits
} sMPEG2_TS_HDR;

	
/* -------------------------------------------------
/       sMPEG2_TS_PAYLOAD 
/		MPEG2 Transport Stream Payload
/--------------------------------------------------*/
typedef struct{
	UINT8		payload[184];
	
} sMPEG2_TS_PAYLOAD;


/* -------------------------------------------------
/       sMPEG2_TS
/		MPEG2 Transport Stream = HDR(4 bytes) + PAYLOAD(184 bytes)
/--------------------------------------------------*/
typedef struct{
	sMPEG2_TS_HDR		hdr;
	sMPEG2_TS_PAYLOAD	payload;
} sMPEG2_TS;



/* -------------------------------------------------
/       sPES
/		PES(Packetized Elementary Stream)
/--------------------------------------------------*/
typedef struct{
	UINT8		prefix1;
	UINT8		prefix2;
	UINT8		prefix3;	///< packet start code prefix fixed value = 0x000001
	UINT8		stream;		///< stream id, audio stream(0xC0-0xDF), video stream(0xE0-0xEF)
} sPES;


/* -------------------------------------------------
/       sPES_EXT
/		PES_Option_Header(Packetized Elementary Stream Extension) 
/--------------------------------------------------*/
typedef struct{
	UINT16		length;		///< PES packet length(The number of PES packet) ,8 bits
	UINT8		flag1;		///< 10						 ,2 bits
					///< PES scrambling control			 ,2 bits
					///< PES priority				 ,1 bit
					///< data alignment indicator			 ,1 bit
					///< copyright					 ,1 bit
					///< original or copy				 ,1 bit

	UINT8		flag2;		///< PTS_DTS_flag(Presentation/Decode time stamp),2 bits
					///< ESCR_flag					 ,1 bit
					///< ES rate flag				 ,1 bit
					///< DSM_trick_mode_flag			 ,1 bit
					///< additional_copy_info_flag			 ,1 bit
					///< PES_CRC_flag				 ,1 bit
					///< PES_extension_flag			         ,1 bit
} sPES_EXT;



/* -------------------------------------------------
/       sPES_EXT2
/		PES_Option_Header(Packetized Elementary Stream Extension 2) 
/--------------------------------------------------*/
typedef struct{
	UINT8		hdr_len;	///< PES header data length(the total number of bytes occupied by the optional fields 
					///  and any stuffing bytes contained in this PES packet header.)

} sPES_EXT2;


/* -------------------------------------------------
/       sPI_PORTAL_PKT
/		RTP_HDR+Payload  TODO
/--------------------------------------------------*/
typedef struct{
	UINT32		hdr;
	UINT8		payload[PIRACAST_PKT_PAYLOAD_SIZE_MAX];
} sPI_PORTAL_PKT;	
	

/* -------------------------------------------------
/       sSLICE_HDR
/		SLICE HEADER TODO 
/--------------------------------------------------*/
#define SLICE_TYPE_PCR		0
#define SLICE_TYPE_SLICE	1

typedef struct{
	UINT8		type;
	UINT8		rsvd[3];
	UINT64		timestamp;

} sSLICE_HDR;

#endif
