#ifndef SMT_PROTO_H
#define SMT_PROTO_H

#include "avformat.h"
#include "libavutil/opt.h"
#include "libavutil/log.h"
#include "libavutil/fifo.h"
#include "network.h"
#include <stdbool.h>
#include "url.h"
#ifdef SMT_PROTOCAL_SIGNAL
#include "smt_signal.h"
#endif


#define MTU 1452     //ip 20 + udp 8 + mtp 1452 = 1480      1500
#define MAX_ASSET_NUMBER 10
#define MAX_AGGGREGATION_DU_NUMBER 1000
#define MIN_PACKET_SIZE 8

#define SMT_PACKET_HEAD_LENGTH 16
#define SMT_PACKET_HEAD_EXTENSION_LENGTH 4
#define SMT_MPU_PAYLOAD_HEAD_LENGTH 8
#define SMT_MPU_PAYLOAD_DU_HEAD_LENGTH 14
#define SMT_GFD_PAYLOAD_HEAD_LENGTH 12
#define SMT_SIG_PAYLOAD_HEAD_LENGTH 2
#define SMT_ID_PAYLOAD_HEAD_LENGTH 13
#define SMT_SOURCE_FEC_PAYLOAD_ID_LENGTH 8
#define SMT_MAX_DELIVERY_NUM    10

#define MEDIA_TRACK_ID_OFFSET 1
#define BUFFER_TIME 1000

#define FIXED_UDP_LEN
#define SMT_NET_STATE

#define EDIT_LIST_NUM 2

typedef enum fec_type{
    no_fec = 0,
    al_fec_source,
    al_fec_repair,
    reserved
} smt_fec_type;

typedef enum payload_type{
    mpu_payload = 0,
    gfd_payload,
    sig_payload,
    repair_symbol_payload,
#ifdef SMT_NET_STATE
    net_state   = 0x20
#endif
} smt_payload_type;

typedef enum fragment_type{
    none = -1,
	mpu_metadata = 0,
	movie_fragment_metadata,
	mfu,
	reserved3,
	reserved4,
	reserved5,
	reserved6,
	reserved7,
	reserved8,
	reserved9,
	reserved10,
	reserved11,
	reserved12,
	reserved13,
	reserved14,
	reserved15	
} smt_fragment_type;

typedef enum fragmentation_indicator{
	complete_data = 0,
	first_fragment,
	middle_fragment,
	last_fragment
} smt_fragmentation_indicator;

typedef  struct payload_mpu {
	unsigned short					length; // payload length except itself
	smt_fragment_type				FT; //  0:mpu metadata 1:movie fragment metadata 2:mfu 3~15: reserved
	bool							T;
	smt_fragmentation_indicator		f_i; // indicate fragment position
	bool							A; //indicate if data is aggrated.
	unsigned char					frag_counter; // number of fragments in one payload
	unsigned int					MPU_sequence_number; //MPU seq
	unsigned short					DU_length[MAX_AGGGREGATION_DU_NUMBER];
	struct{
		unsigned int				movie_fragment_sequence_number;
		unsigned int				sample_number;
		unsigned int				offset;
		unsigned char				priority;
		unsigned char				dep_counter;
        unsigned int                Item_ID;
	}                               DU_Header[MAX_AGGGREGATION_DU_NUMBER];
    unsigned char*                  data;
    unsigned int                    data_len;
} smt_payload_mpu;

typedef struct payload_sig {
	smt_fragmentation_indicator		f_i;
	unsigned char					res;
	bool							H;
	bool							A;
	unsigned char					frag_counter;
	unsigned short					MSG_length;
    unsigned char*                  data;
    unsigned int                    data_len;
} smt_payload_sig;

#ifdef SMT_NET_STATE
typedef struct payload_netstate {
    int64_t                         delivery_time;
    unsigned char                   device_info[128];
    unsigned char*                  data;
    unsigned int                    data_len;
} smt_payload_netstate;
#endif

typedef struct payload_id {
    unsigned int                    SS_start;
    unsigned int                    RSB_length;
    unsigned int                    RS_ID;
    unsigned int                    SSB_length;
    unsigned char*                  data;
    unsigned int                    data_len;
    struct {
        bool                        TS_Indicator;
        unsigned int                FP_TS;
    }                               FFSRP_TS;
} smt_payload_id;

typedef struct payload_gfd {
    bool                            C;
    bool                            L;
    bool                            B;
    char                            CP;
    char                            RES;
    int                             TOI;
    unsigned long long              start_offset;
    unsigned char*                  data;
    unsigned int                    data_len;
} smt_payload_gfd;

typedef union payload {
    smt_payload_mpu                 mpu;
    smt_payload_sig                 signal;
    smt_payload_id                  id;
    smt_payload_gfd                 gfd;
} smt_payload;



typedef struct packet{
    short                           V; // version number of smt protocol
    bool                            C; //"1" means all packets should be counted, "0" means not
    smt_fec_type                    FEC; // 0: no fec. 1: source symbol 2: repair symbol 3: for future usage
    bool                            r; 
    bool                            X; // indicate extension
	bool							R;
	short							RES;
    smt_payload_type                type; // refer to the payload type
    unsigned short                  packet_id; // indicate the stream type. 0: audio 1: video
    unsigned int                    timestamp; // indicate the sending timestamp
    unsigned int                    packet_sequence_number; // indicate the packet sequence number
    unsigned int                    packet_counter; // indicate packet counter
    struct {
        unsigned short              type;
        unsigned short              length;
        unsigned char               *header_extension_value;
    }                               header_extension;
    smt_payload                     payload;
    struct {
        unsigned int                SS_ID;
        unsigned int                FFSRP_TS;
    }                               Source_FEC_payload_ID; 
    struct packet                   *previous, *next;
    bool                            dummy;
} smt_packet;


typedef enum smt_status{
    SMT_STATUS_INIT,
    SMT_STATUS_OK,
    SMT_STATUS_ERROR,
    SMT_STATUS_NEED_MORE_DATA,
    SMT_STATUS_HAS_MORE_DATA,
    SMT_STATUS_NOT_SUPPORT,
    SMT_STATUS_INVALID_INPUT
} smt_status;

typedef enum smt_parse_phase {
   SMT_PARSE_NOT_START,
   SMT_PARSE_PACKET_HEADER,
   SMT_PARSE_PACKET_HEADER_EXTENSION,
   SMT_PARSE_PAYLOAD_HEADER,
   SMT_ALLOC_PAYLOAD_DATA,
   SMT_PARSE_FEC_TAIL,
   SMT_PARSE_PACKET_FINISH
} smt_parse_phase;

typedef struct mpu {
	unsigned char*  mpu_header_data;
	unsigned int	mpu_header_length;
	unsigned char*  moof_header_data;
	unsigned int	moof_header_length;
    unsigned char*	sample_data;
	unsigned int	sample_length;
    unsigned int    offset;
    int             asset;
    int             sequence;
    int64_t         media_duration;
    int32_t         timescale;
/*
	struct sample{
		unsigned char*	sample_data;
		unsigned int	sample_length;
	}				*sample;
    unsigned char*  sample_size;
*/
    struct mpu      *next;
} smt_mpu;

#ifdef SMT_PROTOCAL_SIGNAL
typedef struct sig {
    u_int16_t message_id;
    u_int8_t version;
    u_int32_t length;// new version
    u_int8_t number_of_tables;
    table_header_t *table_header;
    pa_table_t pa_table ;
    mp_table_t mp_table;
    mpi_table_t mpi_table;
} smt_sig;
#else
typedef struct sig {
    unsigned char * sample_data;
    unsigned int    sample_length;
} smt_sig;
#endif

typedef struct gfd {
    unsigned char*	sample_data;
	unsigned int	sample_length;
} smt_gfd;

typedef struct id {
    unsigned char*	sample_data;
	unsigned int	sample_length;
} smt_id;


typedef struct callback {
    void         (* mpu_callback_fun)(URLContext *h, smt_mpu *mpu);
    void         (* sig_callback_fun)(URLContext *h, smt_sig *sig);
    void         (* gfd_callback_fun)(URLContext *h, smt_gfd *gfd);
    void         (* id_callback_fun) (URLContext *h, smt_id *id);
    int          (* packet_send)     (URLContext *h, unsigned char *buf, int len);
    int64_t      (* set_begin_time)  (URLContext *h, unsigned short packet_id, int64_t begin_time);// packet_id indicate the stream type. 0: audio 1: video
    int64_t      (* get_begin_time)  (URLContext *h, unsigned short packet_id); // packet_id indicate the stream type. 0: audio 1: video
    int          (* set_last_packet_counter)(URLContext *h, unsigned int);
    unsigned int (* get_last_packet_counter)(URLContext *h);
    int64_t      (* on_mpu_lost)  (URLContext *h, unsigned short packet_id, int64_t count);// packet_id indicate the stream type. 0: audio 1: video
} smt_callback;

typedef struct smt_receive_entity{
    bool             stack_init;
    smt_status       packet_parser_status;
    smt_parse_phase  packet_parse_phase;
    unsigned char*   packet_buffer;
    unsigned int     packet_buffer_data_len;
    int              process_position;
    smt_packet*      mpu_head[MAX_ASSET_NUMBER];
    unsigned int     need_more_data, has_more_data;
    smt_packet*      current_packet;
    int              packet_counter;
}smt_receive_entity;

typedef struct smt_send_entity{
    int              asset;
    int              pkt_counter;
    int              mpu_seq[MAX_ASSET_NUMBER];
    int              pkt_seq[MAX_ASSET_NUMBER];
    int              moof_index;
    int              sample_index;
} smt_send_entity;

smt_status smt_parse(URLContext *h, smt_receive_entity *recv, unsigned char* buffer, int *p_size);
void smt_release_mpu(URLContext *h, smt_mpu *mpu);
smt_status smt_pack_mpu(URLContext *h, smt_send_entity *snd, unsigned char* buffer, int length);
#ifdef SMT_NET_STATE
int smt_pack_net_state(unsigned char** buffer);
#endif
#ifdef SMT_PROTOCAL_SIGNAL
smt_status smt_pack_signal(URLContext *h);
long signalling_message_segment_append(signalling_message_buf_t *p_signalling_message, void *data,  u_int32_t length);
int id_change(edit_list_t edit_list_id,int id_new,int mpu_new);
int info_change(int id_new,int mpu_new);
#endif
extern int SMT_FD[SMT_MAX_DELIVERY_NUM];
extern int nb_smt_fd;

#endif

