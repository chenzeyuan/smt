#include <time.h>
#include "smt_proto.h"
#include "avformat.h"
#include "libavutil/avassert.h"

//#define SMT_DUMP

/*for receive*/
static bool             stack_init = false;
static smt_status       packet_parser_status;
static smt_parse_phase  packet_parse_phase;
static unsigned char*   packet_buffer = NULL;
static unsigned int     packet_buffer_data_len = 0;
static int              process_position;
static smt_packet*      mpu_head[MAX_ASSET_NUMBER];
static unsigned int     need_more_data = 0, has_more_data = 0;
static smt_packet*      current_packet;
static int				packet_counter;
extern smt_callback     smt_callback_entity;

/*for send*/
static int				asset;
static int				pkt_counter = 0;
static int 				mpu_seq[MAX_ASSET_NUMBER] = {0};
static int              pkt_seq[MAX_ASSET_NUMBER] = {0};
static int				moof_index;
static int				sample_index;


static smt_status smt_parse_mpu_payload(URLContext *h, unsigned char* buffer, smt_payload_mpu **p)
{
    unsigned int data_len = 0, aggregation_du_index = 0;
    unsigned int remained_len = 0;
    unsigned int offset = 0;
    smt_payload_mpu *payload = *p;
    if(packet_parse_phase == SMT_PARSE_PAYLOAD_HEADER){
        if(packet_buffer_data_len - process_position < SMT_MPU_PAYLOAD_HEAD_LENGTH){
            packet_parser_status = SMT_STATUS_NEED_MORE_DATA;
            need_more_data = SMT_MPU_PAYLOAD_HEAD_LENGTH - (packet_buffer_data_len - process_position);
            return SMT_STATUS_NEED_MORE_DATA;
        }
        payload->length = buffer[0] << 8 | buffer[1];
        switch((buffer[2] >> 4) & 0x0f){
            case 0x00:
                payload->FT = mpu_metadata;
                break;
            case 0x01:
                payload->FT = movie_fragment_metadata;
                break;
            case 0x02:
                payload->FT = mfu;
                break;
            default:
                packet_parser_status = SMT_STATUS_INIT;
                return SMT_STATUS_NOT_SUPPORT;
        }
        payload->T = (buffer[2] >> 3) & 0x01;
        switch((buffer[2] >> 1) & 0x03){
            case 0x00:
                payload->f_i = complete_data;
                break;
            case 0x01:
                payload->f_i = first_fragment;
                break;
            case 0x02:
                payload->f_i = middle_fragment;
                break;
            case 0x03:
                payload->f_i = last_fragment;
                break;
        }
        payload->A = buffer[2] & 0x01;
        payload->frag_counter = buffer[3];
        payload->MPU_sequence_number = (buffer[4] << 24) | (buffer[5] << 16) | (buffer[6] << 8) | buffer[7];
		process_position += SMT_MPU_PAYLOAD_HEAD_LENGTH;
		offset = SMT_MPU_PAYLOAD_HEAD_LENGTH;
        packet_parse_phase = SMT_ALLOC_PAYLOAD_DATA;
    }

    if(packet_parse_phase == SMT_ALLOC_PAYLOAD_DATA){
        if(packet_buffer_data_len - process_position < (payload->length - SMT_MPU_PAYLOAD_HEAD_LENGTH + 2)){
            packet_parser_status = SMT_STATUS_NEED_MORE_DATA;
            need_more_data = (payload->length - SMT_MPU_PAYLOAD_HEAD_LENGTH + 2) - (packet_buffer_data_len - process_position);
            return SMT_STATUS_NEED_MORE_DATA;
        }
        if(payload->A){
            data_len = payload->length - SMT_MPU_PAYLOAD_HEAD_LENGTH + 2;
            payload->data = (unsigned char*)malloc(data_len);
            memset(payload->data, 0, data_len);
            while(data_len > 0){
                payload->DU_length[aggregation_du_index] = ((buffer+offset)[0] << 8) | (buffer+offset)[1];
                offset += 2;
				process_position += 2;
                if(payload->FT == mfu){
                    payload->DU_Header[aggregation_du_index].movie_fragment_sequence_number = ((buffer+offset)[0] << 24) | ((buffer+offset)[1] << 16) | ((buffer+offset)[2] << 8) | (buffer+offset)[3];
                    payload->DU_Header[aggregation_du_index].sample_number = ((buffer+offset)[4] << 24) | ((buffer+offset)[5] << 16) | ((buffer+offset)[6] << 8) | (buffer+offset)[7];
                    payload->DU_Header[aggregation_du_index].offset = ((buffer+offset)[8] << 24) | ((buffer+offset)[9] << 16) | ((buffer+offset)[10] << 8) | (buffer+offset)[11];
                    payload->DU_Header[aggregation_du_index].priority = (buffer+offset)[12];
                    payload->DU_Header[aggregation_du_index].dep_counter = (buffer+offset)[13];
                    offset += 14;
					process_position += 14;
                }
                memcpy(payload->data + payload->data_len, buffer + offset, payload->DU_length[aggregation_du_index]);
                payload->data_len += payload->DU_length[aggregation_du_index];
                offset += payload->DU_length[aggregation_du_index];
                process_position += payload->DU_length[aggregation_du_index];
                aggregation_du_index++;
                if(aggregation_du_index > MAX_AGGGREGATION_DU_NUMBER){
                    av_log(h, AV_LOG_ERROR, "aggregated payload number is more than MAX number %d!\n", MAX_AGGGREGATION_DU_NUMBER);
                    return SMT_STATUS_NOT_SUPPORT;
                }
                data_len -= (20 + payload->DU_length[aggregation_du_index]);         
            }
        }else{
            if(payload->FT == mfu){
                payload->DU_Header[aggregation_du_index].movie_fragment_sequence_number = ((buffer+offset)[0] << 24) | ((buffer+offset)[1] << 16) | ((buffer+offset)[2] << 8) | (buffer+offset)[3];
                payload->DU_Header[aggregation_du_index].sample_number = ((buffer+offset)[4] << 24) | ((buffer+offset)[5] << 16) | ((buffer+offset)[6] << 8) | (buffer+offset)[7];
                payload->DU_Header[aggregation_du_index].offset = ((buffer+offset)[8] << 24) | ((buffer+offset)[9] << 16) | ((buffer+offset)[10] << 8) | (buffer+offset)[11];
                payload->DU_Header[aggregation_du_index].priority = (buffer+offset)[12];
                payload->DU_Header[aggregation_du_index].dep_counter = (buffer+offset)[13];
                offset += 14;
				process_position += 14;
				data_len = payload->length - (SMT_MPU_PAYLOAD_HEAD_LENGTH + SMT_MPU_PAYLOAD_DU_HEAD_LENGTH) + 2;
            }else
            	data_len = payload->length - SMT_MPU_PAYLOAD_HEAD_LENGTH + 2;
            
            payload->data = (unsigned char*)malloc(data_len);
            memset(payload->data, 0, data_len);
            memcpy(payload->data, buffer + offset, data_len);
            offset += data_len;
            payload->data_len = data_len;
            process_position += data_len;
        }
    }
    //av_log(h, AV_LOG_WARNING, "pld: mpu number: %d, fragment type = %d, f_i = %d \n", payload->MPU_sequence_number, payload->FT, payload->f_i);
    return SMT_STATUS_OK;
}

static smt_status smt_parse_gfd_payload(URLContext *h, unsigned char* buffer, unsigned int size, smt_payload_gfd **p)
{
    smt_payload_gfd *payload = *p;
    if(packet_parse_phase == SMT_PARSE_PAYLOAD_HEADER){
        if(size < SMT_GFD_PAYLOAD_HEAD_LENGTH){
            packet_parser_status = SMT_STATUS_NEED_MORE_DATA;
            need_more_data = SMT_GFD_PAYLOAD_HEAD_LENGTH - size;
            return SMT_STATUS_NEED_MORE_DATA;
        }
        payload->C = (buffer[0] >> 7) & 0x01;
        payload->L = (buffer[0] >> 6) & 0x01;
        payload->B = (buffer[0] >> 5) & 0x01;
        payload->CP = ((buffer[0] & 0x1f) << 3) | ((buffer[1] >> 5) & 0x0f);
        payload->RES = buffer[1] & 0x1f;
        payload->TOI = (buffer[2] << 24) | (buffer[3] << 16) | (buffer[4] << 8) | buffer[5];
        payload->start_offset = (buffer[6] << 40) | (buffer[7] << 32) | (buffer[8] << 24) | (buffer[9] << 16) | (buffer[10] << 8) | buffer[11];
        process_position += SMT_PARSE_PAYLOAD_HEADER;
        packet_parse_phase = SMT_ALLOC_PAYLOAD_DATA;
    }
    if(packet_parse_phase == SMT_ALLOC_PAYLOAD_DATA){
        payload->data = (unsigned char*)malloc(size - SMT_PARSE_PAYLOAD_HEADER); // smt protocol has some problem for GFD mode. If length of sending packet is more than MTU, we have no way to get real length of payload data.
        memset(payload->data, 0, size - SMT_PARSE_PAYLOAD_HEADER);
        memcpy(payload->data, buffer + SMT_PARSE_PAYLOAD_HEADER, size - SMT_PARSE_PAYLOAD_HEADER);
        process_position += (size - SMT_PARSE_PAYLOAD_HEADER);
    }
    return SMT_STATUS_OK;
}

static smt_status smt_parse_sig_payload(URLContext *h, unsigned char* buffer, int size, smt_payload_sig **p)
{
    unsigned int   offset = 0, data_len = 0;
    smt_payload_sig *payload = *p;

    if(packet_parse_phase == SMT_PARSE_PAYLOAD_HEADER){
        if(size < SMT_GFD_PAYLOAD_HEAD_LENGTH){
            packet_parser_status = SMT_STATUS_NEED_MORE_DATA;
            need_more_data = SMT_GFD_PAYLOAD_HEAD_LENGTH - size;
            return SMT_STATUS_NEED_MORE_DATA;
        }
        switch((buffer[0] >> 6) & 0x03){
            case 0x00:
                payload->f_i = complete_data;
                break;
            case 0x01:
                payload->f_i = first_fragment;
                break;
            case 0x02:
                payload->f_i = middle_fragment;
                break;
            case 0x03:
                payload->f_i = last_fragment;
                break;
        }
        payload->res = (buffer[0] >> 2) & 0x0f;
        payload->H = (buffer[0] >> 1) & 0x01;
        payload->A = buffer[0] & 0x01;
        payload->frag_counter = buffer[1];
        if(payload->A && payload->frag_counter){
            av_log(h, AV_LOG_ERROR, "aggregated payload can not count fragments!");
            packet_parser_status = SMT_STATUS_INIT;
            return SMT_STATUS_ERROR;
        }
		process_position += SMT_SIG_PAYLOAD_HEAD_LENGTH;
		offset = SMT_SIG_PAYLOAD_HEAD_LENGTH;
        packet_parse_phase = SMT_ALLOC_PAYLOAD_DATA;
    }
    
    if(packet_parse_phase == SMT_ALLOC_PAYLOAD_DATA){
        while(offset < size){
                if(payload->A){
                    if(payload->H){
                        payload->MSG_length = (buffer[2] << 8) | buffer[3];
						process_position += 2;
                        offset += 2;
                    }else{
                        payload->MSG_length = buffer[2];
						process_position += 1;
                        offset += 1;
                    }
                    data_len += payload->MSG_length;
                    payload->data = (unsigned char*)realloc(payload->data, data_len);
                    if(!payload->data){
                        av_log(h, AV_LOG_FATAL, "can not realloc memory for signaling message!");
                        packet_parser_status = SMT_STATUS_INIT;
                        return SMT_STATUS_ERROR; 
                    }
                    memset(payload->data, 0, data_len);
                    memcpy(payload->data, buffer + offset, data_len);
					process_position += data_len;
                    offset += data_len;
                }else{
                    payload->data_len = size - offset;
                    payload->data = (unsigned char*)malloc(size - offset);
                    memset(payload->data, 0, payload->data_len);
                    memset(payload->data, buffer+offset, payload->data_len);
					process_position += payload->data_len;
                    offset += payload->data_len;
                }
            }


    }
    
    return SMT_STATUS_OK;
}

static smt_status smt_parse_repair_payload(URLContext *h, unsigned char* buffer, int size, smt_payload_id **p)
{
    unsigned int   offset = 0;
    smt_payload_id *payload = *p;
    if(packet_parse_phase == SMT_PARSE_PAYLOAD_HEADER){
        if(size < SMT_ID_PAYLOAD_HEAD_LENGTH){
            packet_parser_status = SMT_STATUS_NEED_MORE_DATA;
            need_more_data = SMT_ID_PAYLOAD_HEAD_LENGTH - size;
            return SMT_STATUS_NEED_MORE_DATA;
        }
        payload->SS_start = (buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] << 8) | buffer[3];
        payload->RSB_length = (buffer[4] << 16) | (buffer[5] << 8) | buffer[6];
        payload->RS_ID = (buffer[7] << 16) | (buffer[8] << 8) | buffer[9];
        payload->SSB_length = (buffer[10] << 16) | (buffer[11] << 8) | buffer[12];
		process_position += SMT_ID_PAYLOAD_HEAD_LENGTH;
		offset = SMT_ID_PAYLOAD_HEAD_LENGTH;
        packet_parse_phase = SMT_ALLOC_PAYLOAD_DATA;
    }

    if(packet_parse_phase == SMT_ALLOC_PAYLOAD_DATA){
        if(size < MTU + 4){
            packet_parser_status = SMT_STATUS_NEED_MORE_DATA;
            need_more_data = MTU + 4 - size;
            return SMT_STATUS_NEED_MORE_DATA;
        }
        payload->data = (unsigned char *)malloc(MTU);
        memset(payload->data, 0, MTU);
        memcpy(payload->data, buffer + offset, MTU);
		process_position += MTU;
        offset += MTU;
        payload->FFSRP_TS.TS_Indicator = ((buffer + offset)[0] >> 7) & 0x01; 
        payload->FFSRP_TS.FP_TS = (((buffer + offset)[0] << 24) & 0x7F) | ((buffer + offset)[1] << 16) | ((buffer + offset)[2] << 8) | (buffer + offset)[3];
        offset += 4;
        process_position += 4;

    }

    return SMT_STATUS_OK;
}


static smt_status smt_parse_packet(URLContext *h, unsigned char* buffer, int size, smt_packet *p)
{
    smt_status ret = SMT_STATUS_OK;
    smt_payload *payload;
    bool    loop_end = false;
    if(!buffer || !size){
        av_log(h, AV_LOG_ERROR, "input error. buffer = %u,size = %d!\n",buffer, size);
        return SMT_STATUS_ERROR;
    }
	
    do{
        switch(packet_parser_status){
            case SMT_STATUS_INIT:{
                packet_buffer = (unsigned char *)malloc(size);
                memset(packet_buffer, 0, size);
                memcpy(packet_buffer, buffer, size);
                packet_buffer_data_len = size;
                if(packet_buffer_data_len < SMT_PACKET_HEAD_LENGTH){
                    packet_parser_status = SMT_STATUS_NEED_MORE_DATA;
                    need_more_data = SMT_PACKET_HEAD_LENGTH - packet_buffer_data_len;
                    return SMT_STATUS_NEED_MORE_DATA;
                }else{
                    packet_parse_phase = SMT_PARSE_PACKET_HEADER;
                    packet_parser_status = SMT_STATUS_OK;
                }
                break;
            }
            case SMT_STATUS_NEED_MORE_DATA:{
                packet_buffer = (unsigned char *)realloc(packet_buffer, packet_buffer_data_len + size);
                memcpy(packet_buffer+packet_buffer_data_len, buffer, size);
                packet_buffer_data_len += size;
                if(size < need_more_data){
                    need_more_data = need_more_data - size;
                    return SMT_STATUS_NEED_MORE_DATA;
                }else
                    packet_parser_status = SMT_STATUS_OK;
                break;
            }
            case SMT_STATUS_HAS_MORE_DATA:{
                packet_buffer = (unsigned char *)realloc(packet_buffer, packet_buffer_data_len + size);
                memcpy(packet_buffer+packet_buffer_data_len, buffer, size);
                packet_buffer_data_len += size;
                if(packet_buffer_data_len < SMT_PACKET_HEAD_LENGTH){
                    packet_parser_status = SMT_STATUS_NEED_MORE_DATA;
                    need_more_data = SMT_PACKET_HEAD_LENGTH - packet_buffer_data_len;
                    return SMT_STATUS_NEED_MORE_DATA;
                }else{
                    packet_parse_phase = SMT_PARSE_PACKET_HEADER;
                    packet_parser_status = SMT_STATUS_OK;
                }
                break;
            }
            case SMT_STATUS_OK:{
                if(packet_parse_phase == SMT_PARSE_PACKET_HEADER){
                    p->V = (packet_buffer[0] >> 6) & 0x03;
                    if(p->V != 0){
                        av_log(h, AV_LOG_ERROR, "only smt verion 0 is supported! current version: %d\n",p->V);
                        return SMT_STATUS_ERROR;
                    }
                    
                    p->C = (packet_buffer[0] >> 5) & 0x01;
                    switch((packet_buffer[0] >> 3) & 0x03){
                        case 0x00: 
                            p->FEC = no_fec; 
                            break;
                        case 0x01: 
                            p->FEC = al_fec_source; 
                            break;
                        case 0x02: 
                            p->FEC = al_fec_repair; 
                            break;
                        case 0x03: 
                            p->FEC = reserved; 
                            packet_parser_status = SMT_STATUS_INIT;
                            av_log(h, AV_LOG_ERROR, "reserved FEC flag is not supported now.");
                            return SMT_STATUS_NOT_SUPPORT;
                    }
                    p->r = (packet_buffer[0] >> 2) & 0x01;
                    p->X = (packet_buffer[0] >> 1) & 0x01;
                    p->R = packet_buffer[0] & 0x01;
                    if(p->R){
                        av_log(h, AV_LOG_WARNING, "Random access is not supported!");
                        packet_parser_status = SMT_STATUS_INIT;
                        return SMT_STATUS_NOT_SUPPORT;
                    }
                    p->RES = (packet_buffer[1] >> 6) & 0x03;
                    switch(packet_buffer[1] & 0x3f){
                        case  0x00:
                            p->type = mpu_payload;
                            break;
                        case 0x01:
                            p->type = gfd_payload;
                            break;
                        case 0x02:
                            p->type = sig_payload;
                            break;
                        case 0x03:
                            p->type = repair_symbol_payload;
                            break;
                        default:
                            packet_parser_status = SMT_STATUS_INIT;
                            av_log(h, AV_LOG_ERROR, "wrong packet type. type = %d\n",packet_buffer[1] & 0x3f);
                            return SMT_STATUS_NOT_SUPPORT;
                    }
                    
                    p->packet_id = (packet_buffer[2] << 8) | packet_buffer[3];
                    p->timestamp = (packet_buffer[4] << 24) | (packet_buffer[5] << 16) | (packet_buffer[6] << 8) | packet_buffer[7];
                    p->packet_sequence_number = (packet_buffer[8] << 24) | (packet_buffer[9] << 16) | (packet_buffer[10] << 8) | packet_buffer[11];
                    //av_log(h, AV_LOG_INFO, "get packet number = %d\n",p->packet_sequence_number);
                    p->packet_counter = (packet_buffer[12] << 24) | (packet_buffer[13] << 16) | (packet_buffer[14] << 8) | packet_buffer[15];
                    process_position += 16;
                    packet_parse_phase = SMT_PARSE_PACKET_HEADER_EXTENSION;
                }
                
                if(packet_parse_phase == SMT_PARSE_PACKET_HEADER_EXTENSION){
                    if(p->X){
                        if(packet_buffer_data_len - process_position < SMT_PACKET_HEAD_EXTENSION_LENGTH){
                            packet_parser_status = SMT_STATUS_NEED_MORE_DATA;
                            need_more_data = SMT_PACKET_HEAD_EXTENSION_LENGTH - (packet_buffer_data_len - process_position);
                            return SMT_STATUS_NEED_MORE_DATA;
                        }
                        if(!p->header_extension.type && !p->header_extension.length){
                            p->header_extension.type = (packet_buffer[16] << 8) | packet_buffer[17];
                            p->header_extension.length = (packet_buffer[18] << 8) | packet_buffer[19];
                            process_position += 4;
                        }
        
                        if(packet_buffer_data_len - process_position < p->header_extension.length){
                            packet_parser_status = SMT_STATUS_NEED_MORE_DATA;
                            need_more_data = p->header_extension.length - (packet_buffer_data_len - process_position);
                            return SMT_STATUS_NEED_MORE_DATA;
                        }
                        
                        process_position += p->header_extension.length; //skip this field
                    }
                    packet_parse_phase = SMT_PARSE_PAYLOAD_HEADER;
                }
        
        
                payload = &(p->payload);
                switch(p->type){
                    case mpu_payload:{
                        smt_payload_mpu *mpu = (smt_payload_mpu *)payload;
                        ret = smt_parse_mpu_payload(h, packet_buffer + process_position, &mpu);
                        break;
                    }case gfd_payload:{
                        smt_payload_gfd *gfd = (smt_payload_gfd *)payload;
                        ret = smt_parse_gfd_payload(h, packet_buffer + process_position, packet_buffer_data_len - process_position, &gfd);
                        break;
                    }case sig_payload:{
                        smt_payload_sig *sig = (smt_payload_sig *)payload;
                        ret = smt_parse_sig_payload(h, packet_buffer + process_position, packet_buffer_data_len - process_position, &sig);
                        break;
                    }case repair_symbol_payload:{
                        smt_payload_id *id = (smt_payload_id *)payload;
                        ret = smt_parse_repair_payload(h, packet_buffer + process_position, packet_buffer_data_len - process_position, &id);
                        break;
                    }
                }
        
                if(ret == SMT_STATUS_OK){
                    if(p->FEC == al_fec_source && (p->type != repair_symbol_payload))
                        packet_parse_phase = SMT_PARSE_FEC_TAIL;
                }
        
                if(packet_parse_phase == SMT_PARSE_FEC_TAIL){
                    if(packet_buffer_data_len - process_position <  SMT_SOURCE_FEC_PAYLOAD_ID_LENGTH){
                        packet_parser_status = SMT_STATUS_NEED_MORE_DATA;
                        need_more_data = SMT_SOURCE_FEC_PAYLOAD_ID_LENGTH - (packet_buffer_data_len - process_position);
                        return SMT_STATUS_NEED_MORE_DATA;
                    }
                    p->Source_FEC_payload_ID.SS_ID = ((packet_buffer + process_position)[0] << 24) | ((packet_buffer + process_position)[1] << 16) | ((packet_buffer + process_position)[2] << 8) | (packet_buffer + process_position)[3];
                    p->Source_FEC_payload_ID.FFSRP_TS = ((packet_buffer + process_position)[4] << 24) | ((packet_buffer + process_position)[5] << 16) | ((packet_buffer + process_position)[6] << 8) | (packet_buffer + process_position)[7];
                    process_position += 8;
                }
            loop_end = true;
            break;
            }
        }

    }while(!loop_end);


	if(ret == SMT_STATUS_OK){
		av_assert0(process_position <= packet_buffer_data_len);
#ifdef FIXED_UDP_LEN
        if(process_position < packet_buffer_data_len)
            while(!*(packet_buffer+process_position)){
                process_position++;
                if(process_position == packet_buffer_data_len)
                    break;
            }
#endif
		has_more_data = packet_buffer_data_len - process_position;
		if(has_more_data){
			packet_parser_status = SMT_STATUS_HAS_MORE_DATA;
			memmove(packet_buffer, packet_buffer+process_position, has_more_data);
			packet_buffer = (unsigned char *)realloc(packet_buffer, has_more_data);
			return SMT_STATUS_HAS_MORE_DATA;
		}else{
			packet_parse_phase = SMT_PARSE_NOT_START;
			packet_parser_status = SMT_STATUS_INIT;
			free(packet_buffer);
			packet_buffer = NULL;
		}
	}else if(ret == SMT_STATUS_NEED_MORE_DATA){
		has_more_data = 0;
	}
/*
    av_log(h, AV_LOG_WARNING, "pkt: sequence number: %d, asset id = %d, counter = %d, size = %d, timestamp = %d \n",
        p->packet_sequence_number,
        p->packet_id,
        p->packet_counter,
        size,
        p->timestamp);
*/
    return ret;
}

static int smt_find_field(char *buf, int buf_len, char *field, int field_len)
{
	int pos = 0;
	if(buf_len < field_len)
		return -1;
	while(pos <= buf_len - field_len){
		if(!strncmp(buf+pos, field, field_len))
			return pos;
		pos++;
	}

	return -1;
}

static smt_status smt_assemble_mpu(URLContext *h,int asset_id, smt_mpu *mpu)
{
    smt_packet *iterator;
	unsigned int mpu_head_pkt_first_seq,mpu_head_pkt_last_seq, moov_head_pkt_first_seq, moov_head_pkt_last_seq;
    unsigned int mpu_h_offset = 0, moof_h_offset = 0, data_offset = 0;
	int sample_index = 0;
	enum phase_status{
		phase_error = -1,
		phase_mpu_meta_begin,
		phase_mpu_meta_continue,
		phase_moof_meta_begin,
		phase_moof_meta_continue,
		phase_mfu
	} status = phase_mpu_meta_begin;

	iterator = mpu_head[asset_id];
	unsigned int expected_pkt_seq = iterator->packet_sequence_number;
	
    while(iterator){
        smt_payload_mpu *pld = (smt_payload_mpu *)&(iterator->payload);
        switch(status){
			case phase_mpu_meta_begin:{
				if(expected_pkt_seq != iterator->packet_sequence_number){
					av_log(h, AV_LOG_ERROR, "mpu metadata packets %d ~ %d are lost. \n", expected_pkt_seq, iterator->packet_sequence_number - 1);
					return SMT_STATUS_ERROR;
				}
				if(pld->FT == mpu_metadata){
					if(pld->f_i == complete_data){
                        mpu_head_pkt_first_seq = mpu_head_pkt_last_seq = iterator->packet_sequence_number;
						status = phase_moof_meta_begin;
					}else if(pld->f_i == first_fragment){
					    mpu_head_pkt_first_seq = iterator->packet_sequence_number;
						status = phase_mpu_meta_continue;
					}else{
						av_log(h, AV_LOG_ERROR, "first payload fragmentation indicator number is %d, mpu metadata is incomplete!\n", pld->f_i);
						return SMT_STATUS_ERROR;
					}	
				}else{
					av_log(h, AV_LOG_ERROR, "first payload MPU fragement type is %d, no mpu MPU metadata!\n", pld->FT);
					return SMT_STATUS_ERROR;			
				}
				mpu->mpu_header_length += pld->data_len;
				expected_pkt_seq++;
	        	iterator = iterator->next;
				break;
			}case phase_mpu_meta_continue:{
				if(expected_pkt_seq != iterator->packet_sequence_number){
					av_log(h, AV_LOG_ERROR, "mpu metadata packets %d ~ %d are lost. \n", expected_pkt_seq, iterator->packet_sequence_number - 1);
					return SMT_STATUS_ERROR;
				}
				if(pld->FT == mpu_metadata){
					if(pld->f_i == middle_fragment)
						status = phase_mpu_meta_continue;
					else if(pld->f_i == last_fragment){
                        mpu_head_pkt_last_seq = iterator->packet_sequence_number;
						status = phase_moof_meta_begin;
					}else{
						av_log(h, AV_LOG_ERROR, "phase_mpu_meta_continue expect %d or %d but %d \n", middle_fragment, last_fragment, pld->f_i);
						return SMT_STATUS_ERROR;
					}
				}else{
					av_log(h, AV_LOG_ERROR, "%d in phase_mpu_meta_continue\n", pld->FT);
					return SMT_STATUS_ERROR;
				}
				mpu->mpu_header_length += pld->data_len;
				expected_pkt_seq++;
	        	iterator = iterator->next;
				break;
			}case phase_moof_meta_begin:{
				if(expected_pkt_seq != iterator->packet_sequence_number){
					av_log(h, AV_LOG_ERROR, "MOOF metadata packets %d ~ %d are lost. \n", expected_pkt_seq, iterator->packet_sequence_number - 1);
					return SMT_STATUS_ERROR;
				}
				if(pld->FT == movie_fragment_metadata){
					if(pld->f_i == complete_data){
                        moov_head_pkt_first_seq = moov_head_pkt_last_seq = iterator->packet_sequence_number;
						status = phase_mfu;
					}else if(pld->f_i == first_fragment){
					    moov_head_pkt_first_seq = iterator->packet_sequence_number;
						status = phase_moof_meta_continue;
					}else{
						av_log(h, AV_LOG_ERROR, "first payload fragmentation indicator number is %d, moof metadata is incomplete!\n", pld->f_i);
						return SMT_STATUS_ERROR;
					}
				}else{
					av_log(h, AV_LOG_ERROR, "first payload MOOF fragement type is %d, no mpu MOOV metadata!\n", pld->FT);
					return SMT_STATUS_ERROR;
				}
				mpu->moof_header_length += pld->data_len;
				expected_pkt_seq++;
	        	iterator = iterator->next;
				break;
			}case phase_moof_meta_continue:{
				if(expected_pkt_seq != iterator->packet_sequence_number){
					av_log(h, AV_LOG_ERROR, "MOOF metadata packets %d ~ %d are lost. \n", expected_pkt_seq, iterator->packet_sequence_number - 1);
					return SMT_STATUS_ERROR;
				}
				if(pld->FT == movie_fragment_metadata){
					if(pld->f_i == middle_fragment)
						status = phase_moof_meta_continue;
					else if(pld->f_i == last_fragment){
                        moov_head_pkt_last_seq = iterator->packet_sequence_number;
						status = phase_mfu;
					}else{
						av_log(h, AV_LOG_ERROR, "phase_moof_meta_continue expect %d or %d but %d \n", middle_fragment, last_fragment, pld->f_i);
						return SMT_STATUS_ERROR;
					}
				}else{
					av_log(h, AV_LOG_ERROR, "%d in phase_moof_meta_continue\n", pld->FT);
					return SMT_STATUS_ERROR;
				}
				mpu->moof_header_length += pld->data_len;
				expected_pkt_seq++;
	        	iterator = iterator->next;
				break;
			}case phase_mfu:{
			    int sample_size, default_sample_size;
				if(!mpu->mpu_header_data || !mpu->moof_header_data){
					// assmeble mpu header and moof header
					int seq = mpu_head[asset_id]->packet_sequence_number;
					smt_packet *it = mpu_head[asset_id];
					mpu->mpu_header_data = (unsigned char *)malloc(mpu->mpu_header_length);
					memset(mpu->mpu_header_data, 0, mpu->mpu_header_length);
					mpu->moof_header_data = (unsigned char *)malloc(mpu->moof_header_length);
					memset(mpu->moof_header_data, 0, mpu->moof_header_length);
					
					while(seq <= moov_head_pkt_last_seq){
						smt_payload_mpu *pld = (smt_payload_mpu *)(&it->payload);
						if(seq >= mpu_head_pkt_first_seq && seq <= mpu_head_pkt_last_seq){
							memcpy(mpu->mpu_header_data + mpu_h_offset, pld->data, pld->data_len);
							mpu_h_offset += pld->data_len;
						}else if(seq >= moov_head_pkt_first_seq && seq <= moov_head_pkt_last_seq){
							memcpy(mpu->moof_header_data + moof_h_offset, pld->data, pld->data_len);
							moof_h_offset += pld->data_len;
						}
						it = it->next;
						seq = it->packet_sequence_number;
					}

                    //check track id in mpu header     
					int tkhd_offset = smt_find_field(mpu->mpu_header_data ,mpu->mpu_header_length,"tkhd", 4);
					if(tkhd_offset >= 0){
						//av_log(h, AV_LOG_WARNING, "tkhd_offset = %x\n", tkhd_offset);
						unsigned char* tkhd = mpu->mpu_header_data + tkhd_offset; // the offset from beginning to tkhd is 0xb9 in mpu format file.
						int track_id_offset = 0;
						int version = (tkhd+0x04)[0];
						if(version)
							track_id_offset = 0x18;
						else
							track_id_offset = 0x10;
						unsigned char *track_id_ptr = tkhd + track_id_offset;
						int track_id = (track_id_ptr[0] << 24 | track_id_ptr[1] << 16 | track_id_ptr[2] << 8 | track_id_ptr[3]);
						track_id_ptr[0] = (asset_id + MEDIA_TRACK_ID_OFFSET) >> 24;
						track_id_ptr[1] = (asset_id + MEDIA_TRACK_ID_OFFSET) >> 16;
						track_id_ptr[2] = (asset_id + MEDIA_TRACK_ID_OFFSET) >> 8;
						track_id_ptr[3] = (asset_id + MEDIA_TRACK_ID_OFFSET);
				 	}

					int trex_offset = smt_find_field(mpu->mpu_header_data+tkhd_offset,mpu->mpu_header_length-tkhd_offset,"trex", 4);
					if(trex_offset >= 0){
						trex_offset += tkhd_offset;
						//av_log(h, AV_LOG_WARNING, "trex_offset = %x\n", trex_offset);
						unsigned char* trex = mpu->mpu_header_data + trex_offset; // the offset from beginning to tkhd is 0xb9 in mpu format file.
						unsigned char *track_id_ptr = trex + 0x08;
						int track_id = (track_id_ptr[0] << 24 | track_id_ptr[1] << 16 | track_id_ptr[2] << 8 | track_id_ptr[3]);
						track_id_ptr[0] = (asset_id + MEDIA_TRACK_ID_OFFSET) >> 24;
						track_id_ptr[1] = (asset_id + MEDIA_TRACK_ID_OFFSET) >> 16;
						track_id_ptr[2] = (asset_id + MEDIA_TRACK_ID_OFFSET) >> 8;
						track_id_ptr[3] = (asset_id + MEDIA_TRACK_ID_OFFSET);
				 	}


					
                    //check track id in moof again
                    int tfhd_offset = smt_find_field(mpu->moof_header_data ,mpu->moof_header_length,"tfhd", 4);
					if(tkhd_offset >= 0){
						//av_log(h, AV_LOG_WARNING, "tfhd_offset = %x\n", trex_offset);
						unsigned char* tfhd = mpu->moof_header_data + tfhd_offset; //  the offset from moof to tfhd is 0x24 in mpu format file.
						int flag = (tfhd[5] << 16) | (tfhd[6] << 8) | tfhd[7];
						int track_id_offset = 0x08;
                        int default_sample_size_offset = track_id_offset + 4;
                        if(flag & 0x01)
                            default_sample_size_offset += 8; //base time offset field
                        if(flag & 0x08)
                            default_sample_size_offset += 4; //default sample duration field 
                        
						unsigned char *track_id_ptr = tfhd + track_id_offset;
                        unsigned char *dafault_sample_size_ptr = tfhd + default_sample_size_offset;
                        
						//int track_id = (track_id_ptr[0] << 24 | track_id_ptr[1] << 16 | track_id_ptr[2] << 8 | track_id_ptr[3]);
						track_id_ptr[0] = (asset_id + MEDIA_TRACK_ID_OFFSET) >> 24;
						track_id_ptr[1] = (asset_id + MEDIA_TRACK_ID_OFFSET) >> 16;
						track_id_ptr[2] = (asset_id + MEDIA_TRACK_ID_OFFSET) >> 8;
						track_id_ptr[3] = (asset_id + MEDIA_TRACK_ID_OFFSET);

                        default_sample_size = (dafault_sample_size_ptr[0] << 24 | dafault_sample_size_ptr[1] << 16 | dafault_sample_size_ptr[2] << 8 | dafault_sample_size_ptr[3]);
					}
					//prepare sample buffer
					unsigned char *mdat_size = mpu->moof_header_data + (mpu->moof_header_length - 8);
					mpu->sample_length = (mdat_size[0] << 24 | mdat_size[1] << 16 | mdat_size[2] << 8 | mdat_size[3]) - 8; //exclude mdat header
					mpu->sample_data = (unsigned char *)malloc(mpu->sample_length);
					memset(mpu->sample_data, 0, mpu->sample_length);
				}

				int trun_offset = smt_find_field(mpu->moof_header_data ,mpu->moof_header_length,"trun", 4);
				unsigned char *trun = mpu->moof_header_data + trun_offset; // trun offset - moof offset = 0x40
				int flag = (trun[5] << 16) | (trun[6] << 8) | trun[7];
				unsigned char *buf = trun + 0x0c;
				if(flag & 0x01)
					buf += 4;
				if(flag & 0x04)
					buf += 4;

                if(flag & 0x200){
    				int ext_duration = (flag & 0x100) ? 4 : 0;
                    int ext_flags = (flag & 0x400) ? 4: 0;
                    int ext_cts = (flag & 0x800) ? 4: 0;
    				int offset = (4 + ext_duration + ext_flags + ext_cts) * sample_index + ext_duration ;  //skip sample duration, flags and cts if fields exist
    				sample_size = buf[offset] << 24 | buf[offset+1] << 16 | buf[offset+2] << 8 | buf[offset+3];
                }else{
                    sample_size = default_sample_size;
                }
                int sample_process_finish = 1;
			
				if(sample_index < pld->DU_Header[0].sample_number - 1){ //sample number is started from 1
					av_log(h, AV_LOG_WARNING, "packets %d ~ %d are lost. all packets in sample %d lost\n",
						iterator->previous->packet_sequence_number + 1, 
						iterator->packet_sequence_number - 1,
						sample_index);
				}else if(sample_index == pld->DU_Header[0].sample_number - 1){
					if(pld->f_i == complete_data){
						av_assert0(sample_size == pld->data_len);
						memcpy(mpu->sample_data + data_offset, pld->data, sample_size); //copy from packet directly
						iterator = iterator->next;
					}else if(pld->f_i == first_fragment){
						unsigned char *sample_buf = (unsigned char *)malloc(sample_size);
						int good_sample = 0;
						int pos = 0;
						do{
							memcpy(sample_buf + pos, pld->data, pld->data_len);
							pos += pld->data_len;
							int seq = iterator->packet_sequence_number;
							iterator = iterator->next;
							if(pld->f_i == last_fragment)
								break;
							if(!iterator)
								break;
							if(iterator->packet_sequence_number - seq == 1){
								good_sample = 1;
							}else if(iterator->packet_sequence_number - seq > 1){
								av_log(h, AV_LOG_WARNING, "packets %d ~ %d are lost.\n",
									iterator->previous->packet_sequence_number + 1, 
									iterator->packet_sequence_number - 1);
								good_sample = 0;
                                if(pld->DU_Header[0].sample_number - 1 == sample_index)
                                    sample_process_finish = 0;
							}else{
								av_log(h, AV_LOG_ERROR, "packet sequence(%d) can not smaller than previous(%d).\n", iterator->packet_sequence_number, seq);
								av_assert0(0);
							}
							pld = (smt_payload_mpu *)&(iterator->payload);
						}while(good_sample);

						if(good_sample){  //only copy good sample
							av_assert0(pos == sample_size || !iterator);
							memcpy(mpu->sample_data + data_offset, sample_buf, sample_size);
                            //av_log(h, AV_LOG_INFO, "sample %d is a good sample, sample_size = %d\n",sample_index, sample_size);
						}
						free(sample_buf);
                        sample_buf = NULL;
					}else{
						int counter = 0;
						while(pld->f_i != first_fragment && pld->f_i != complete_data){ //skip useless packets
							iterator = iterator->next;
							if(!iterator)
								return SMT_STATUS_OK;
							pld = (smt_payload_mpu *)&(iterator->payload);
							counter++;
						}
						av_log(h, AV_LOG_WARNING, "packets for sample %d of %s MPU %d is lost. %d packets are skipped \n", 
                            sample_index, asset_id?"video":"audio", pld->MPU_sequence_number, counter - 1);
					}

				}else{
					av_assert0(0);
				}

                if(sample_process_finish){
    				data_offset += sample_size;
    				sample_index++;
                }
				break;
			}
		}
		
    }

    return SMT_STATUS_OK;

}

static void smt_release_buffer(URLContext *h, int asset)
{
	smt_packet *iterator = mpu_head[asset];
	while(iterator){
		smt_packet *tmp = iterator->next;
		switch(iterator->type){
			case mpu_payload:{
				smt_payload_mpu *pld = (smt_payload_mpu *)(&iterator->payload);
				if(pld->data){
					free(pld->data);
                    pld->data = NULL;
                }
				break;
			}case gfd_payload:{
				smt_payload_gfd *pld = (smt_payload_gfd *)(&iterator->payload);
				if(pld->data){
					free(pld->data);
                    pld->data = NULL;
                }
				break;
			}case sig_payload:{
				smt_payload_sig *pld = (smt_payload_sig *)(&iterator->payload);
				if(pld->data){
					free(pld->data);
                    pld->data = NULL;
                }
				break;
			}case repair_symbol_payload:{
				smt_payload_id *pld = (smt_payload_id *)(&iterator->payload);
				if(pld->data){
					free(pld->data);
                    pld->data = NULL;
                }
				break;
			}
		}
		free(iterator);
		iterator = tmp;
	}
}

static smt_status smt_add_mpu_packet(URLContext *h, smt_packet *p)
{
    int asset_id = p->packet_id;
    smt_status ret = SMT_STATUS_OK;
    if(!mpu_head[asset_id]){
        mpu_head[asset_id] = p;
    }else{
        smt_payload_mpu *pld_f, *pld;
        pld_f = (smt_payload_mpu *)&(mpu_head[asset_id]->payload);
        pld = (smt_payload_mpu *)&(p->payload);
        if(pld_f->MPU_sequence_number < pld->MPU_sequence_number){
                smt_mpu *mpu = (smt_mpu *)malloc(sizeof(smt_mpu));
                memset(mpu, 0, sizeof(smt_mpu));
                mpu->asset = asset_id;
                mpu->sequnce = pld_f->MPU_sequence_number;
                ret = smt_assemble_mpu(h,asset_id, mpu);
                if(ret != SMT_STATUS_OK){
                    av_log(h, AV_LOG_ERROR, "assemble mpu %d failed!\n\n", pld_f->MPU_sequence_number);
                }else{
#ifdef SMT_DUMP
                    char fn[256];
                    memset(fn, 0, 256);
                    sprintf(fn, "../../../../Temp/mpu_%d_%s.mpu", pld_f->MPU_sequence_number, asset_id?"v":"a");
                    avformat_dump(fn, mpu->mpu_header_data, mpu->mpu_header_length, "a+");
                    avformat_dump(fn, mpu->moof_header_data, mpu->moof_header_length, "a+");
                    avformat_dump(fn, mpu->sample_data, mpu->sample_length, "a+");
                    av_log(h, AV_LOG_INFO, "%s is generated\n",fn, pld->MPU_sequence_number, asset_id);
#endif
                    if(smt_callback_entity.mpu_callback_fun)
                        smt_callback_entity.mpu_callback_fun(h,mpu);
                    else
                        smt_release_mpu(h, mpu);
                }
                
                smt_release_buffer(h, asset_id);
                mpu_head[asset_id] = p;
                return ret;
        }
        
        if(p->packet_sequence_number < mpu_head[asset_id]->packet_sequence_number){
            p->next = mpu_head[asset_id];
            mpu_head[asset_id]->previous = p;
            mpu_head[asset_id] = p;
        }else{
            smt_packet *iterator = mpu_head[asset_id];
            do{
                if(p->packet_sequence_number == iterator->packet_sequence_number){
                    av_log(h, AV_LOG_ERROR, "duplicate packet sequence number. seq_num = %d\n", p->packet_sequence_number);
                    return SMT_STATUS_ERROR;
                }

                if(iterator->next == NULL){
                    iterator->next = p;
                    p->previous = iterator;
                    return SMT_STATUS_OK;
                }else if(iterator->packet_sequence_number < p->packet_sequence_number && iterator->next->packet_sequence_number > p->packet_sequence_number){
                    p->next = iterator->next;
                    iterator->next->previous = p;
                    p->previous = iterator;
                    iterator->next = p;
                    return SMT_STATUS_OK;
                }
                iterator = iterator->next;
            }while(iterator);
        }
    }
    return ret;

}

static smt_status smt_add_packet(URLContext *h, smt_packet *p)
{
    smt_status ret = SMT_STATUS_OK;
    int pkt_id = p->packet_id;
    smt_payload_type tp = p->type;
    if(pkt_id >= MAX_ASSET_NUMBER){
        av_log(h, AV_LOG_ERROR, "current asset number is %d, which is bigger than MAX_ASSET_NUMBER!\n", pkt_id);
        packet_parser_status = SMT_STATUS_INIT;
        return SMT_STATUS_NOT_SUPPORT;
    }
    
    switch(tp){
        case mpu_payload:
            ret = smt_add_mpu_packet(h, p);
            break;
        case gfd_payload:
            break;
        case sig_payload:
        case repair_symbol_payload:
            break;
    }
    return ret;
}

static smt_status smt_assemble_packet_header(URLContext *h, unsigned char *buffer, smt_packet *pkt)
{

    buffer[0] = (pkt->R&0x01) | (pkt->X&0x01) << 1 | (pkt->r&0x01) << 2 | (pkt->FEC&0x03) << 3 | (pkt->C&0x01) << 5 | (pkt->V&0x03) << 6;
    buffer[1] = (pkt->type&0x3f) | (pkt->RES&0x03)<<6;
	buffer[2] = (unsigned char)(pkt->packet_id >> 8);
	buffer[3] = (unsigned char)(pkt->packet_id);
	buffer[4] = (unsigned char)(pkt->timestamp >> 24);
	buffer[5] = (unsigned char)(pkt->timestamp >> 16); 
	buffer[6] = (unsigned char)(pkt->timestamp >> 8); 
	buffer[7] = (unsigned char)(pkt->timestamp);
	buffer[8] = (unsigned char)(pkt->packet_sequence_number >> 24);
	buffer[9] = (unsigned char)(pkt->packet_sequence_number >> 16); 
	buffer[10] = (unsigned char)(pkt->packet_sequence_number >> 8); 
	buffer[11] = (unsigned char)(pkt->packet_sequence_number);
	buffer[12] = (unsigned char)(pkt->packet_counter >> 24);
	buffer[13] = (unsigned char)(pkt->packet_counter >> 16); 
	buffer[14] = (unsigned char)(pkt->packet_counter >> 8); 
	buffer[15] = (unsigned char)(pkt->packet_counter);
	if(pkt->X && pkt->header_extension.header_extension_value) {
		buffer[16] = (unsigned char)(pkt->header_extension.type>> 8);
		buffer[17] = (unsigned char)(pkt->header_extension.type);
		buffer[18] = (unsigned char)(pkt->header_extension.length>> 8);
		buffer[19] = (unsigned char)(pkt->header_extension.length);
		memset(buffer + 19, 0, pkt->header_extension.length);
		memcpy(buffer + 19, pkt->header_extension.header_extension_value, pkt->header_extension.length);
	}
    pkt_counter++;
	return 0;
}

static smt_status smt_assemble_payload_header(URLContext *h, unsigned char *buffer, smt_payload_mpu *pld)
{
	buffer[0] = (unsigned char)(pld->length >> 8);
	buffer[1] = (unsigned char)(pld->length);
	buffer[2] = (pld->A&0x01) | (pld->f_i&0x03) << 1 | (pld->T&0x01) << 3 | (pld->FT&0x0f) << 4;
	buffer[3] = (unsigned char)pld->frag_counter;
	buffer[4] = (unsigned char)((0xff000000 & pld->MPU_sequence_number) >> 24);
	buffer[5] = (unsigned char)((0xff0000 & pld->MPU_sequence_number) >> 16);
	buffer[6] = (unsigned char)((0xff00 & pld->MPU_sequence_number) >> 8);
	buffer[7] = (unsigned char)(0xff & pld->MPU_sequence_number);
    if(pld->A == 0){
    	if(pld->FT == mfu) {
    		buffer[8]  = (unsigned char)((0xff000000 & pld->DU_Header[0].movie_fragment_sequence_number) >> 24);
    		buffer[9]  = (unsigned char)((  0xff0000 & pld->DU_Header[0].movie_fragment_sequence_number) >> 16);
    		buffer[10] = (unsigned char)((    0xff00 & pld->DU_Header[0].movie_fragment_sequence_number) >> 8);
    		buffer[11] = (unsigned char)(       0xff & pld->DU_Header[0].movie_fragment_sequence_number);
    		buffer[12] = (unsigned char)((0xff000000 & pld->DU_Header[0].sample_number) >> 24);
    		buffer[13] = (unsigned char)((  0xff0000 & pld->DU_Header[0].sample_number) >> 16);
    		buffer[14] = (unsigned char)((    0xff00 & pld->DU_Header[0].sample_number) >> 8);
    		buffer[15] = (unsigned char)(       0xff & pld->DU_Header[0].sample_number);
    		buffer[16] = (unsigned char)((0xff000000 & pld->DU_Header[0].offset) >> 24);
    		buffer[17] = (unsigned char)((  0xff0000 & pld->DU_Header[0].offset) >> 16);
    		buffer[18] = (unsigned char)((    0xff00 & pld->DU_Header[0].offset) >> 8);
    		buffer[19] = (unsigned char)(       0xff & pld->DU_Header[0].offset);
    		buffer[20] = (unsigned char)pld->DU_Header[0].priority;
    		buffer[21] = (unsigned char)pld->DU_Header[0].dep_counter;
    	}
    }else
        av_log(h, AV_LOG_ERROR, "payload aggregation is not supported now!");
	return 0;
}


smt_status smt_parse(URLContext *h, unsigned char* buffer, int *p_size)
{
    smt_status status;
    unsigned char *payload_data = NULL;
    smt_packet *packet;
    int size = *p_size;

    if(!buffer || !size)
        return SMT_STATUS_INVALID_INPUT;

    if(!stack_init){
        stack_init = true;
        memset(mpu_head, 0, sizeof(smt_packet *)*10);
    }
    if(packet_parser_status == SMT_STATUS_NEED_MORE_DATA){
        packet = current_packet;
    }else{
        packet_parse_phase = SMT_PARSE_NOT_START;
        if(packet_parser_status == SMT_STATUS_HAS_MORE_DATA)
            packet_buffer_data_len = has_more_data;
        else
            packet_buffer_data_len = 0;
        process_position = 0;
        packet = (smt_packet *)malloc(sizeof(smt_packet));
        memset(packet, 0, sizeof(smt_packet));
    }
    
    status = smt_parse_packet(h, buffer, size, packet);
    switch(status){
        case SMT_STATUS_OK:
            packet_parse_phase = SMT_PARSE_NOT_START;
            status = smt_add_packet(h, packet);
            break;
        case SMT_STATUS_HAS_MORE_DATA:
            status = smt_add_packet(h, packet);
            break;
        case SMT_STATUS_NEED_MORE_DATA:
            *p_size = need_more_data;
            current_packet = packet;
            break;
        case SMT_STATUS_NOT_SUPPORT:
        case SMT_STATUS_ERROR:
            if(packet_parse_phase == SMT_ALLOC_PAYLOAD_DATA){
                switch(packet->type){
                    case mpu_payload:{
                        smt_payload_mpu *mpu = (smt_payload_mpu *)(&packet->payload);
                        payload_data = mpu->data;
                        break;
                    }case gfd_payload:{
                        smt_payload_gfd *gfd = (smt_payload_gfd *)(&packet->payload);
                        payload_data = gfd->data;
                        break;
                    }case sig_payload:{
                        smt_payload_sig *sig = (smt_payload_sig *)(&packet->payload);
                        payload_data = sig->data;
                        break;
                    }case repair_symbol_payload:{
                        smt_payload_id *id = (smt_payload_id *)(&packet->payload);
                        payload_data = id->data;
                        break;
                    }    
                }
            }
            if(payload_data){
                free(payload_data);
                payload_data = NULL;
            }
            free(packet);
            break;
    }
    return status;
}

void smt_release_mpu(URLContext *h, smt_mpu *mpu)
{
    //int i = 0;
    if(!mpu)
        return;
    if(mpu->mpu_header_data){
        free(mpu->mpu_header_data);
        mpu->mpu_header_data = NULL;
    }
    if(mpu->moof_header_data){
        free(mpu->moof_header_data);
        mpu->moof_header_data = NULL;
    }
    if(mpu->sample_data){
        free(mpu->sample_data);
        mpu->sample_data = NULL;
    }
/*
    if(!mpu->sample)
        return;
    for(;i<mpu->sample_size;i++){
        if(mpu->sample[i].sample_data)
            free(mpu->sample[i].sample_data);
    }
    free(mpu->sample);
*/
    free(mpu);
    mpu = NULL;
}


smt_status smt_pack_mpu(URLContext *h, unsigned char* buffer, int length)
{
	unsigned char *tag = buffer+4;
	int position = 0;
	smt_packet *pkt = (smt_packet *)malloc(sizeof(smt_packet));
    memset(pkt, 0, sizeof(smt_packet));
	smt_payload_mpu *pld = (smt_payload_mpu *)&(pkt->payload);
	int size, offset = 0, ext = 0;
    smt_status status;
	//initialization
	pkt->V = 0;
	pkt->C = 1;
	pkt->FEC = no_fec;
	pkt->r = 0;
	pkt->X = 0;
	pkt->R = 0;
	pkt->RES = 0;
	pkt->type = mpu_payload;	
	pld->T = 1;
	pld->A = 0;
	pld->DU_Header[0].priority = 0;
	pld->DU_Header[0].dep_counter = 0;
	pld->data = (unsigned char *)malloc(MTU);

	
	int type = MKTAG(tag[0],tag[1],tag[2],tag[3]);    
	if(type == MKTAG('f','t','y','p')){
		int hdlr_offset, mmpu_offset;
		unsigned char *hdlr, *mmpu;
		int media;
		hdlr_offset = smt_find_field(buffer,length,"hdlr", 4);
        if(hdlr_offset < 0)
            return SMT_STATUS_NEED_MORE_DATA;
		hdlr = buffer + hdlr_offset;
		media = MKTAG((hdlr + 0x0c)[0], (hdlr + 0x0c)[1], (hdlr + 0x0c)[2], (hdlr + 0x0c)[3]);
		if(media == MKTAG('v','i','d','e'))
			asset = 1;
		else if(media == MKTAG('s','o','u','n'))
			asset = 0;
        mmpu_offset = buffer[0] << 24 | buffer[1] << 16 | buffer[2] << 8 | buffer[3];
		mmpu = buffer + mmpu_offset;
		mpu_seq[asset] = (mmpu + 0x0d)[0] << 24 | (mmpu + 0x0d)[1] << 16 | (mmpu + 0x0d)[2] << 8 | (mmpu + 0x0d)[3];
		moof_index = -1;
		sample_index = 0;
		pld->FT = mpu_metadata;
	}else if(type == MKTAG('m','o','o','f')){
		moof_index++;
		pld->FT = movie_fragment_metadata;
	}else{
		sample_index++;
		pld->FT = mfu;
	}


	pkt->packet_id = asset;
	pld->MPU_sequence_number = mpu_seq[asset];
	pld->DU_Header[0].movie_fragment_sequence_number = moof_index;
	pld->DU_Header[0].sample_number = sample_index;
    offset = SMT_PACKET_HEAD_LENGTH + SMT_MPU_PAYLOAD_HEAD_LENGTH;
	if(pld->FT == mfu)
		offset += SMT_MPU_PAYLOAD_DU_HEAD_LENGTH;
    if(pkt->X){
        ext = pkt->header_extension.length + 4;
        offset += ext;
        if(offset >= MTU)
        av_log(h, AV_LOG_ERROR, "UDP MTU is less than SMT header, program crash.\n");
        av_assert0(offset < MTU);
    }
    size = MTU - offset;
	if(length%size == 0)
		pld->frag_counter = length/size;
	else
		pld->frag_counter = length/size + 1;
	if(size >= length)
		pld->f_i = complete_data;
	else
		pld->f_i = first_fragment;
	while(position < length){
        memset(pld->data, 0, MTU);
		pkt->packet_counter = pkt_counter;
        pkt->packet_sequence_number = pkt_seq[asset];
        time_t t;
        time(&t);
        pkt->timestamp = t;
		smt_assemble_packet_header(h, pld->data, pkt);

        if(position > 0){
            if(length - position <= size)
                pld->f_i = last_fragment;
            else
                pld->f_i = middle_fragment;
        }
    
		if(pld->FT == mfu)
			pld->DU_Header[0].offset = position;

		if(pld->f_i == complete_data || pld->f_i == last_fragment){
			pld->length = length - position + SMT_MPU_PAYLOAD_HEAD_LENGTH - 2 + (pld->FT == mfu?SMT_MPU_PAYLOAD_DU_HEAD_LENGTH:0);
            pld->data_len = length - position;
		}else{
			pld->length = MTU - SMT_PACKET_HEAD_LENGTH - 2;
            pld->data_len = size;
        }
        
        smt_assemble_payload_header(h, pld->data + SMT_PACKET_HEAD_LENGTH + ext , pld);
        memcpy(pld->data + offset, buffer + position, pld->data_len);

        //send data
#ifdef FIXED_UDP_LEN
        if(0 > smt_callback_entity.packet_send(h, pld->data, MTU)){
#else
        if(0 > smt_callback_entity.packet_send(h, pld->data, pld->data_len + offset)){
#endif
            av_log(h, AV_LOG_ERROR, "send smt packet failed.\n");
            status = SMT_STATUS_ERROR;
            break;
        }

/*
        av_log(h, AV_LOG_WARNING, "packet send len = %d. asset id = %d, seq = %d, mpu = %d, counter =%d, timestamp = %d, offset = %d\n",
            pld->data_len + offset,
            pkt->packet_id, 
            pkt->packet_sequence_number, 
            pld->MPU_sequence_number,
            pkt->packet_counter,
            pkt->timestamp,
            offset);
*/   
		position += pld->data_len;
        pkt_seq[asset]++;
	}
    free(pld->data);
    free(pkt);
	return SMT_STATUS_OK;
}





