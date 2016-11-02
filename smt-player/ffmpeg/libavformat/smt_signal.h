#ifndef NET_MMT_MMTP_MMT_H_
#define NET_MMT_MMTP_MMT_H_

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <endian.h>//just for little endian
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
//#include "smt_proto.h"
#include "url.h"

#ifdef OS_ANDROID 
#include <android/log.h>
#ifndef LOG_TAG
#define LOG_TAG  "mmt"
#define printf(...) __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)
#endif
#endif
//#define PRINTF(format, ...) print_info(__FILE__, __FUNCTION__,__LINE__); printf(format, ##__VA_ARGS__)
#define PRINTF(format, ...)


#define MP4_FOURCC( a, b, c, d ) \
   ( ((uint32_t)a) | ( ((uint32_t)b) << 8 ) \
   | ( ((uint32_t)c) << 16 ) | ( ((uint32_t)d) << 24 ) )


#define eth_BUFF_LEN 1500

#define IPh_BUFF_LEN 20
#define IP_BUFF_LEN 1480

#define UDPh_BUFF_LEN 8
#define UDP_BUFF_LEN 1452

#define MMTPh_BUFF_LEN 16
#define MMTP_BUFF_LEN 1436

#define signalh_BUFF_LEN 4
#define signal_BUFF_LEN 1432
#define MPUh_BUFF_LEN 8
#define MPU_BUFF_LEN 1428
#define MFUh_t_BUFF_LEN 14
#define MFU_t_BUFF_LEN 1414
#define MFUh_nt_BUFF_LEN 4
#define MFU_nt_BUFF_LEN 1424

#define maximum_value 4294967295
#define SMTh_BUFF_LEN 10

#define PAh_BUFF_LEN 8
#define PAth_BUFF_LEN 4
#define th_BUFF_LEN 4
#define Signal_h_BUFF_LEN 4

#define table_header_LEN 4

#define MAX_SEND_ASSET_NUM 15

//packet id for signaling messages
#define Signal_PACKET_ID  20

//length for asset attribute name
#define MAX_ATTR_LEN 30
#define MAX_PATH_LEN 1500

#define MPU_INTERVAL 500

//##define uint32_t UINT32

typedef struct Asset_info
{
		u_int16_t last_time;
		u_int8_t assets_count;
        char *asset_id;
        char *media_type;
        char *asset_type;
        uint16_t packet_id;
        char *asset_path;
        char *asset_send_begintime;
        char *asset_send_endtime;
}asset_info_t;

typedef struct PI_info
{
		u_int8_t PI_cotent_count;
        char *PI_content_type;
        char *PI_type;
        char *PI_content_name;
        char *PI_content_path;
}PI_info_t;


typedef struct MMT_config //16Byte
{

	char *to_addr;
	int16_t to_port;

//samsung
	int32_t session_num;
	char *mode;
	int16_t listen_port;
	char *multicast_ip;
	int16_t multicast_port;
	int32_t time_delay;
	char *broadband_root;
	char *broadcast_root;
	int32_t fec ;
	int32_t qos ;
	char *log_path;
	char *controller;
	int32_t max_client_num;
	char *res_map_file;
//	char *broadcast_default_ci;
//	char *broadcast_default_html;
	int16_t multiview_port;

}mmt_config_t;

typedef struct MPU_metadata_s //Byte
{
	 u_int32_t start;
	 u_int32_t length;
	 u_int32_t number;
}mpu_metadata_t;

typedef struct Fragment_metadata_s //Byte
{

    u_int32_t start;
    u_int32_t length;
    u_int32_t number;

}fragment_metadata_t;

typedef struct MFU_s //Byte
{

    u_int32_t start;
    u_int32_t length;
    u_int32_t number;

}mfu_t;

typedef struct MMT_packet_header_s //16Byte
{
    u_int8_t version:2;
    u_int8_t packet_counter_flag:1;
    u_int8_t FEC_type:2;
    u_int8_t reserved_1:1;
    u_int8_t extension_flag:1;
    u_int8_t RAP_flag:1;
    u_int8_t reserved_2:2;
    u_int8_t type:6;
    u_int16_t packet_id;
    u_int32_t timestamp;
    u_int32_t packet_sequence_number;
    u_int32_t packet_counter;
}mmt_packet_header_t,*pmmt_packet_header_t;

typedef struct MMT_packet_header_extension_header_s//4Byte
{
	u_int16_t type;
	u_int16_t length;
    //no header extension
    //no FEC
}mmt_packet_header_extension_header_t,*pmmt_packet_header_extension_header_t;


typedef struct MPU_packet_header_s //8Byte
{

	u_int16_t length;
	u_int8_t FT:4;//mpu_fragment_type
	u_int8_t timed_Flag:1;
	u_int8_t f_i:2;//fragmentation_indicator
	u_int8_t aggregation_flag:1;
	u_int8_t fragment_counter;
	u_int32_t MPU_sequence_number;
    //no MFU
    //for non-time

}mpu_packet_header_t,*pmpu_packet_header_t;

typedef struct MFU_time_packet_header_s //14Byte
{
	u_int32_t movie_fragment_sequence_number;
	u_int32_t sample_number;
	u_int32_t offset;
	u_int8_t subsample_priority;
	u_int8_t dependency_counter;
    //for time
}mfu_time_packet_header_t,*pmfu_time_packet_header_t;

typedef struct MFU_non_time_packet_header_s //4Byte
{
	u_int32_t item_ID;
    //for non-time
}mfu_non_time_packet_header_t,*pmfu_non_time_packet_header_t;

typedef struct SMT_packet_header_s //4Byte
{
	u_int16_t SPS;
	u_int8_t SDC;
	u_int16_t LEN;
	u_int16_t TCH;
	u_int16_t RSV1;
	u_int8_t RSV2;
    //for time
}smt_packet_header_t;

typedef struct signal_header_s //4Byte
{
	u_int8_t f_i:2;
	u_int8_t res:4;
	u_int8_t H:1;
	u_int8_t A:1;
	u_int8_t frag_counter;
	u_int16_t MSG_length1;
	u_int32_t MSG_length2;

    //for time
}signal_header_t;

typedef struct table_header_s //4Byte
{
	u_int8_t table_id;
	u_int8_t version;
	u_int16_t length;
}table_header_t;

typedef struct asset_id_s
{
	u_int32_t asset_id_scheme;
	u_int8_t asset_id_length;
	unsigned char *asset_id_byte;
} asset_id_t, *passet_id_t;

typedef struct ID_mapping_URL_s
{
	u_int16_t URL_count;
	u_int16_t *URL_length;
	unsigned char *URL_byte;
}ID_mapping_URL_t,*pID_mapping_URL_t;

typedef union Identifier_mapping_byte_s //4Byte
{
	asset_id_t *asset_id;
	ID_mapping_URL_t *ID_mapping_URL;
//	......
}Identifier_mapping_byte_t, *pIdentifier_mapping_byte_t;

typedef struct Identifier_mapping_s
{
	u_int8_t identifier_type;
	Identifier_mapping_byte_t * identifier_mapping_byte;
}Identifier_mapping_t, *pIdentifier_mapping_t;

typedef union general_location_info_byte_s
{
	u_int16_t packet_id;
	struct
	{
		u_int32_t ipv4_src_addr;
		u_int32_t ipv4_dst_addr;
		u_int16_t dst_port;
		u_int16_t packet_id;
	} location_type01;
//	......

}general_location_info_byte_t, *pGeneral_location_info_byte_t;

typedef struct general_location_info_s
{
	u_int8_t location_type;
	general_location_info_byte_t * general_location_info_byte;
}general_location_info_t, pGeneral_location_info_t;

typedef struct asset_location_s
{
	u_int8_t location_count;
	general_location_info_t *general_location_info;
}asset_location_t, *pAsset_location_t;

typedef struct pa_table_s //4Byte
{
	u_int8_t table_id;
	u_int8_t version;
	u_int16_t length;
	unsigned char *pat_content;
}pa_table_t;

typedef struct MP_table_asset_s
{
	Identifier_mapping_t *Identifier_mapping;
	u_int32_t asset_type;
	u_int8_t reserved:7;
	u_int8_t asset_clock_relation_flag:1;
	asset_location_t *asset_loaction;
	u_int16_t asset_descriptors_length;
	unsigned char *asset_descriptors_byte;

}MP_table_asset_t;

typedef struct edit_list_s
{
	u_int8_t edit_list_id;
	u_int32_t mpu_sequence_number;
}edit_list_t;

typedef struct MUR_descriptors_s
{
	u_int16_t descriptor_tag;
	u_int16_t descriptor_length;
	edit_list_t *edit_list;
}MUR_descriptors_t;

typedef struct level_id_s
{
	u_int16_t level_id;
	u_int8_t level_id_num;
	u_int32_t *level_sequence_num;
}level_id_t;

typedef struct level_s
{
	u_int8_t level_num;
	level_id_t *level_id;
}level_t;

typedef struct mp_table_s //4Byte
{
	u_int8_t table_id;
	u_int8_t version;
	u_int16_t length;
	u_int8_t reserved:6;
	u_int8_t MP_table_mode:2;
	u_int8_t number_of_assets;
	MP_table_asset_t *MP_table_asset;
}mp_table_t;


typedef struct PI_content_s //4Byte
{
	u_int8_t PI_content_type_length;
	unsigned char * PI_content_type_byte;
	//yes
	u_int8_t PI_content_name_length;
	unsigned char * PI_content_name_byte;
	//no use
	u_int8_t PI_content_descriptors_length;
	unsigned char * PI_content_descriptors_byte;
	//yes
	u_int16_t PI_content_length;
	unsigned char * PI_content_byte;
}PI_content_t,*pPI_content_t;

typedef struct MPIT_descriptors_s
{
	u_int16_t descriptors_length;
	unsigned char *MPIT_descriptors_byte;
}MPIT_descriptors_t, *pMPIT_descriptors_t;

typedef struct mpi_table_s //4Byte
{
	u_int8_t table_id;
	u_int8_t version;
	u_int16_t length;
	//bu dui
	u_int8_t reserved1:4;
	u_int8_t PI_mode:2;
	u_int8_t reserved2:2;
	//use wrong
	MPIT_descriptors_t *MPIT_descriptor;
	u_int8_t PI_content_count;
	PI_content_t *PI_content;
}mpi_table_t;

//client

typedef struct pa_message_s //8Byte
{
	u_int16_t message_id;
	u_int8_t version;
	u_int32_t length;// new version
	u_int8_t number_of_tables;
	table_header_t *table_header;
	pa_table_t pa_table ;
	mp_table_t mp_table;
	mpi_table_t mpi_table;
}pa_message_t;

typedef struct signal_buf_s //4Byte
{

	char * buf_pool;
	char * block_fill_flag;
	u_int32_t block_number;
	u_int8_t buff_status; 
	u_int16_t packet_id; 
}signal_buf_t,*psignal_buf_t;

typedef struct signalling_message_buf_s 
{
    unsigned char *signal_buf;
    u_int32_t signal_seekpoint;
    u_int32_t length;
}signalling_message_buf_t,*psignalling_message_buf_t;


typedef struct mfu_buf_s //30Byte
{
	unsigned  char *data;
	u_int8_t flag ;
	u_int32_t length;
	u_int32_t number;
	u_int32_t counter;

}mfu_buf_t,*pmfu_buf_t;

typedef struct item_buf_s
{
	uint16_t item_ID;    /* theses are arrays */
	uint32_t base_offset;
	uint16_t extent_count;
////uint32_t extent_offset;
	uint32_t extent_length;

	unsigned  char *data;
	u_int8_t flag ;
	u_int32_t length;
	u_int32_t number;
	u_int32_t counter;

}item_buf_t,*pitem_buf_t;

// first get sample number
typedef struct mfu_sample_s //30Byte
{
u_int32_t sequence_number;
u_int8_t trackrefindex;
u_int32_t movie_fragment_sequence_number;
u_int32_t sample_number;
u_int8_t priority;
u_int8_t dependency_counter;
u_int32_t offset;
u_int32_t length;
u_int32_t muli_length;
u_int32_t muli_name;
u_int16_t muli_reserved1;
u_int8_t muli_reserved2;

}mfu_sample_t,*pmfu_sample_t;

typedef struct mpu_metadata_buf_s //30Byte
{
	unsigned char *data;
	unsigned char *data_store;
	u_int8_t flag;
	u_int32_t length;
	u_int32_t number;
	u_int32_t counter;

}mpu_metadata_buf_t,*pmpu_metadata_buf_t;

typedef struct fragment_metadata_buf_s //30Byte
{
	unsigned char *data;
	u_int8_t flag ;
	u_int32_t length;
	u_int32_t number;
	u_int32_t counter;

}fragment_metadata_buf_t,*pfragment_metadata_buf_t;

typedef struct mpu_buf_s //
{
	u_int16_t packet_id; 
	u_int32_t MPU_sequence_number; 

	mpu_metadata_buf_t  mpu_metadata_buf;
	fragment_metadata_buf_t fragment_metadata_buf;

	u_int8_t flag ;

	u_int32_t mfu_number;
	u_int32_t mfu_counter;

	u_int16_t item_number;
	uint16_t item_counter;

	item_buf_t *item;
	mfu_buf_t *mfu;
	mfu_sample_t *mfu_sample;
	u_int32_t MPU_sequence_length;
	u_int32_t MPU_sequence_seek;
	unsigned char * MPU_sequence_data;
	int state;
	long receive_time;
}mpu_buf_t,*pmpu_buf_t;

typedef struct mmt_socket_s //
{
	int ss;
	struct sockaddr addr;
}mmt_socket_t,*pmmt_socket_t;



int get_send_timestamp(void);
int get_MMT_timestamp(void);
int init_mmtp_header(mmt_packet_header_t *mmt_header,unsigned char *MMTPh);
int read_mmtp_header(mmt_packet_header_t *mmt_header,const char *MMTPh);
int init_mpu_header(mpu_packet_header_t *mpu_header,unsigned char *MPUh);
int read_mpu_header(mpu_packet_header_t *mpu_header,const char *MPUh);
int init_mfu_time_header(mfu_time_packet_header_t *mfu_time_header,unsigned char *MFUh_t);
int read_mfu_time_header(mfu_time_packet_header_t *mfu_time_header,const char *MFUh_t);
int init_mfu_non_time_header(mfu_non_time_packet_header_t *mfu_non_time_header,unsigned char *MFUh_nt);
int read_mfu_non_time_header(mfu_non_time_packet_header_t *mfu_non_time_header,const char *MFUh_nt);
int init_mfu_sample(mfu_sample_t *mfu_sample,unsigned char *mfu_sample_buf);
int read_mfu_sample(mfu_sample_t *mfu_sample,const char *mfu_sample_buf);

int init_smt_header(smt_packet_header_t *smt_header,unsigned char *SMTh);
int read_smt_header(smt_packet_header_t *smt_header,const char *SMTh);

int init_signal_header(signal_header_t *signal_header,unsigned char *signal_h);
int read_signal_header(signal_header_t *signal_header,const char *signal_h);

int init_pa_message(pa_message_t *pa_header,unsigned char *PAh);
int read_pa_message_header(pa_message_t *pa_header,const char *PAh);
int read_pa_message(pa_message_t *pa_header,const char *PAh);
int free_pa_message(pa_message_t *pa_header);

int init_table_header(table_header_t *table_header ,unsigned char *table_buf);
int read_table_header(table_header_t *table_header ,const char *table_buf);

int init_pa_table(pa_table_t *pa_table ,unsigned char *PA_table_buf);
int read_pa_table(pa_table_t *pa_table ,const char *PA_table_buf);
int copy_mp_table(mp_table_t *mp_table_dst ,mp_table_t *mp_table_src);
int free_pa_table(pa_table_t *pa_table);

int init_mp_table(mp_table_t *mp_table, unsigned char **mp_table_buf);
int make_level_table(level_t *level, char *file);
int readline(FILE *fp, char **line, size_t *size);
int mpu_in_json(u_int32_t MPU_sequence_number,char *file);
//int make_mp_table(mp_table_t* mp_table, asset_info_t* asset_info);
int make_mp_table(mp_table_t* mp_table, asset_info_t* asset_info, char * file, u_int32_t MPU_sequence_number);
int read_MUR_descriptors(MUR_descriptors_t *mur_descriptor,const char *mur_buf);
int read_mp_table(mp_table_t *mp_table ,const char *mp_table_buf);
int get_mp_table_length(mp_table_t *mp_table);
int free_mp_table(mp_table_t *mp_table );

int init_mpi_table(mpi_table_t *mpi_table, unsigned char **mpi_table_buf);
int make_mpi_table(mpi_table_t* mpi_table, PI_info_t* PI_info);
int read_mpi_table(mpi_table_t *mpi_table, const char *mpi_table_buf);
int copy_mpi_table(mpi_table_t *mpi_table_dst, mpi_table_t *mpi_table_src);
int free_mpi_table(mpi_table_t *mpi_table);



//int free_mfu_buf(mfu_buf_t *mfu_buf );
int free_mpu_metadata_buf(mpu_metadata_buf_t  *mpu_metadata_buf );
int free_fragment_metadata_buf(fragment_metadata_buf_t *fragment_metadata_buf );
int free_mpu_buf(mpu_buf_t *mpu_buf );
int init_mpu_buf(mpu_buf_t *mpu_buf );
void print_info(const char *filename, const char *funcname, int linenum);



void generate_and_send_signal(URLContext *h);
int parsexml(char* file, asset_info_t* asset_info_base, PI_info_t* PI_info_base);
#endif
