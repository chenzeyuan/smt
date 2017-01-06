#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>


#include <string.h>
#include <errno.h>
#include <inttypes.h>


#include "smt_proto.h"
#ifdef SMT_PROTOCAL_SIGNAL
#include "smt_signal.h"
#include "smt_getfile.h"
#include "json.h"
#include "parse_flags.h"
#include <libxml/xmlreader.h>
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/tree.h>



#define max_level_id_num 1000
extern smt_callback     smt_callback_entity;


int init_pa_message(pa_message_t *pa_header,unsigned char *PAh)
{

//    memset(PAh,0,PAh_BUFF_LEN );
//7 byte
    *((u_int16_t*)&PAh[0])=htons((u_int16_t)pa_header->message_id);
    *((u_int8_t*)&PAh[2])=pa_header->version;
    *((u_int32_t*)&PAh[3])=htonl(pa_header->length);
    *((u_int8_t*)&PAh[7])=pa_header->number_of_tables;
//    *((u_int8_t*)&MFUh_t[12])=(u_int8_t)mfu_time_header->subsample_priority;
    return 0;

}

int read_pa_message_header(pa_message_t *pa_message,const char *pa_message_buf)
{

    pa_message->message_id=*((u_int16_t*)&pa_message_buf[0]);
    pa_message->version=pa_message_buf[2];
    pa_message->length=ntohl(*((u_int32_t*)&pa_message_buf[3]));
    pa_message->number_of_tables=pa_message_buf[7];

    return 0;
}

int read_pa_message(pa_message_t *pa_message,const char *pa_message_buf)
{
    pa_message->message_id=*((u_int16_t*)&pa_message_buf[0]);
    pa_message->version=pa_message_buf[2];
    pa_message->length=ntohl(*((u_int32_t*)&pa_message_buf[3]));
    pa_message->number_of_tables=pa_message_buf[7];
    pa_message->table_header= (table_header_t *)malloc(sizeof(table_header_t )*pa_message->number_of_tables);
    int i;
    for(i=0;i<pa_message->number_of_tables;i++)
    {
        read_table_header(&pa_message->table_header[i] ,&pa_message_buf[PAh_BUFF_LEN+sizeof(table_header_t )*i]);
    }
    //point to first table
    int buff_seek=PAh_BUFF_LEN+sizeof(table_header_t )*pa_message->number_of_tables;
    for(i=0;i<pa_message->number_of_tables;i++)
    {
        //PA table
        if(pa_message->table_header[i].table_id==0x00)
        {
            read_pa_table(&pa_message->pa_table ,&pa_message_buf[buff_seek]);
        }
        //MP table
        if(pa_message->table_header[i].table_id==0x20)
        {
            read_mp_table(&pa_message->mp_table ,&pa_message_buf[buff_seek]);
        }
        //MPI table
        if(pa_message->table_header[i].table_id==0x10)
        {
            read_mpi_table(&pa_message->mpi_table,&pa_message_buf[buff_seek]);
        }
        //point to next table
        buff_seek=buff_seek+table_header_LEN+pa_message->table_header[i].length;

    }
    return 0;
}

int free_pa_message(pa_message_t *pa_message)
{
    free(pa_message->table_header);
    pa_message->table_header = NULL;
    int i;
    for(i=0;i<pa_message->number_of_tables;i++)
    {
        //PA table
        if(pa_message->table_header[i].table_id==0x10)
        {
            free_pa_table(&pa_message->pa_table);
        }
        //MP table
        if(pa_message->table_header[i].table_id==0x40)
        {
            free_mp_table(&pa_message->mp_table );
        }
        //MPI table
        if(pa_message->table_header[i].table_id==0x20)
        {
            free_mpi_table(&pa_message->mpi_table);
        }
    }
    return 0;
}

int init_table_header(table_header_t *table_header ,unsigned char *table_buf)
{
    memset(table_buf,0,1024);
//4 byte
    *((u_int8_t*)&table_buf[0])=table_header->table_id;
    *((u_int8_t*)&table_buf[1])=table_header->version;
    *((u_int16_t*)&table_buf[2])=htons(table_header->length);
    return 0;

}

int read_table_header(table_header_t *table_header ,const char *table_buf)
{

    table_header->table_id=*((u_int8_t*)&table_buf[0]);
    table_header->version=*((u_int8_t*)&table_buf[1]);
    table_header->length=ntohs(*((u_int16_t*)&table_buf[2]));
    return 0;

}

int init_pa_table(pa_table_t *pa_table ,unsigned char *PA_table_buf)
{
    memset(PA_table_buf,0,1024);
//4 byte
    *((u_int8_t*)&PA_table_buf[0])=pa_table->table_id;
    *((u_int8_t*)&PA_table_buf[1])=pa_table->version;
    *((u_int16_t*)&PA_table_buf[2])=htons(pa_table->length);

    return 0;

}

int read_pa_table(pa_table_t *pa_table ,const char *PA_table_buf)
{

    pa_table->table_id=*((u_int8_t*)&PA_table_buf[0]);
    pa_table->version=*((u_int8_t*)&PA_table_buf[1]);
    pa_table->length=ntohs(*((u_int16_t*)&PA_table_buf[2]));
    pa_table->pat_content=(unsigned  char*) malloc(pa_table->length*sizeof( unsigned  char));
    memcpy(pa_table->pat_content,&PA_table_buf[4],pa_table->length);
    //pa_table->pat_content=*((char *)&PA_table_buf[4]);
    return 0;

}
int free_pa_table(pa_table_t *pa_table )
{

    free(pa_table->pat_content);
    pa_table->pat_content = NULL;
    return 0;

}

int init_mp_table(mp_table_t *mp_table, unsigned char **mp_table_buf)
{
    u_int32_t  seekpoint=6;
    u_int32_t i, j, location_num;
    unsigned char *mp_table_buf_tmp = (unsigned char*) malloc((4+mp_table->length)*sizeof( unsigned   char));
    memset(mp_table_buf_tmp, 0, 4 + mp_table->length);
    if(mp_table_buf_tmp==NULL)
    {
        puts ("Memory allocation failed.");
        exit (EXIT_FAILURE);
    }

    *((u_int8_t*)&mp_table_buf_tmp[0])=mp_table->table_id;
    *((u_int8_t*)&mp_table_buf_tmp[1])=mp_table->version;
    *((u_int16_t*)&mp_table_buf_tmp[2])=htons(mp_table->length);
    *((u_int8_t*)&mp_table_buf_tmp[4]) = (mp_table->reserved<<2)|(mp_table->MP_table_mode);

    *((u_int8_t*)&mp_table_buf_tmp[5])=mp_table->number_of_assets;


    for(i=0;i<mp_table->number_of_assets;i++)
    {
        *((u_int8_t*)&mp_table_buf_tmp[seekpoint]) = mp_table->MP_table_asset[i].Identifier_mapping->identifier_type;
        seekpoint += 1;
        //0x00 for asset_id
        if(mp_table->MP_table_asset[i].Identifier_mapping->identifier_type == 0x00)
        {
            u_int8_t length_tmp = mp_table->MP_table_asset[i].Identifier_mapping->identifier_mapping_byte->asset_id->asset_id_length;
            *((u_int32_t*)&mp_table_buf_tmp[seekpoint]) =  htons(mp_table->MP_table_asset[i].Identifier_mapping->identifier_mapping_byte->asset_id->asset_id_scheme);
            *((u_int8_t*)&mp_table_buf_tmp[seekpoint+4]) = length_tmp;
            seekpoint += 5;
            memcpy(&mp_table_buf_tmp[seekpoint],mp_table->MP_table_asset[i].Identifier_mapping->identifier_mapping_byte->asset_id->asset_id_byte,length_tmp);
            seekpoint += length_tmp;
        }
        //0x01 for URL
        //not handled now
        else if(mp_table->MP_table_asset[i].Identifier_mapping->identifier_type == 0x01)
        {
            seekpoint += 0;
        }
        *((u_int32_t*)&mp_table_buf_tmp[seekpoint]) = htons(mp_table->MP_table_asset[i].asset_type);
        *((u_int8_t*)&mp_table_buf_tmp[seekpoint+4]) = ((mp_table->MP_table_asset[i].reserved)<<1)|(mp_table->MP_table_asset[i].asset_clock_relation_flag);
        *((u_int8_t*)&mp_table_buf_tmp[seekpoint+5]) = mp_table->MP_table_asset[i].asset_loaction->location_count;
        seekpoint += 6;

        location_num = mp_table->MP_table_asset[i].asset_loaction->location_count;
        for(j=0;j<location_num;j++)
        {
            *((u_int8_t*)&mp_table_buf_tmp[seekpoint+1]) = mp_table->MP_table_asset[i].asset_loaction->general_location_info[j].location_type;
            seekpoint += 1;
            //0x00 for packet_id
            if(mp_table->MP_table_asset[i].asset_loaction->general_location_info[j].location_type == 0x00)
            {
                *((u_int16_t*)&mp_table_buf_tmp[seekpoint]) = htons(mp_table->MP_table_asset[i].asset_loaction->general_location_info[j].general_location_info_byte->packet_id);
                seekpoint += 2;
            }
            else if(mp_table->MP_table_asset[i].asset_loaction->general_location_info[j].location_type == 0x01)
            {
                seekpoint += 0;
            }
        }

        *((u_int16_t*)&mp_table_buf_tmp[seekpoint]) = mp_table->MP_table_asset[i].asset_descriptors_length;
        memcpy(&mp_table_buf_tmp[seekpoint+2],mp_table->MP_table_asset[i].asset_descriptors_byte,mp_table->MP_table_asset[i].asset_descriptors_length);
        seekpoint += (2+mp_table->MP_table_asset[i].asset_descriptors_length);
    }

    *mp_table_buf = mp_table_buf_tmp;
    return 0;
}


int make_level_table(level_t* level,char *file)
{
	json_object *new_obj;
	FILE *fp = fopen(file, "rb");
	if(fp == NULL){
		printf("error in open json file\n");
		exit(0);
	}
	char *line = NULL;
	size_t size = 0;
	readline(fp, &line, &size);
	new_obj=json_tokener_parse(line);
	int level_num=json_object_get_int(json_object_object_get(new_obj, "level_num_info"));
	level->level_num=level_num;
	u_int16_t level_id = 0;
	u_int8_t level_id_num = 0;
	level->level_id=(level_id_t *)malloc(level_num*sizeof(level_id_t));
	if (level->level_id==NULL)
	{
		printf("error in malloc level.level_id\n");
		exit(0);
	}
	int i;
	for(i=0;i<level_num;i++)
	{
		readline(fp, &line, &size);
		//printf("line=%s\n",line);
		new_obj=json_tokener_parse(line);
		level_id=json_object_get_int(json_object_object_get(new_obj, "level_id_info"));
		level_id_num=json_object_get_int(json_object_object_get(new_obj, "level_id_num_info"));
		level->level_num=level_num;
		level->level_id[i].level_id=level_id;
		level->level_id[i].level_id_num=level_id_num;
		//printf("level_id=%d\n",level->level_id[i].level_id);
		const char *level_sequence_num=json_object_get_string(json_object_object_get(new_obj,"level_sequence_num_info"));
		int n,j;
		int count=0;
		int level_seq_num[max_level_id_num]; //changed
		//printf("n=%s\n",level_sequence_num);
		level->level_id[i].level_sequence_num=(u_int32_t *)malloc(level->level_id[i].level_id_num*sizeof(u_int32_t));
		if (level->level_id[i].level_sequence_num==NULL)
		{
			printf("error in malloc level.level_id[i].level_sequence_num\n");
			exit(0);
		}
		for(j=0;j<level_id_num;j++)
		{
			level_sequence_num += 1;
			n=atoi(level_sequence_num);

			//level_seq_num[j]=n;
			level->level_id[i].level_sequence_num[j]=n;
			//printf("array=%d\n",level->level_id[i].level_sequence_num[j]);
			count=0;
			if (n==0)
			{
				count=1;
			}
			while(n!=0)
			{
				n=n/10;
				count++;
			}
			level_sequence_num+=count;
		}
//		free(level->level_id[i].level_sequence_num);
//		level->level_id[i].level_sequence_num=NULL;
	}
	fclose(fp);

	return 0;
}

int readline(FILE *fp, char **line, size_t *size)
{
    *size = 0;
    size_t cpos = ftell(fp);

    int c;
    while ((c = fgetc(fp)) != '\n' && c != EOF) (*size)++;
    if (*size == 0 && c == EOF) {
        *line = NULL;
        return 1;
    }

    *line = (char *)calloc(*size + 1, sizeof(char));
    if (*line == NULL)
    {
    	printf("error in calloc *line\n");
    	return -1;
    }

    fseek(fp, cpos, SEEK_SET);
    if (fread(*line, 1, *size, fp) != *size)
    {
    	printf("error in calloc fp\n");
        return -2;
    }
    fgetc(fp); // Skip that newline

    return 0;
}

int mpu_in_json(u_int32_t MPU_sequence_number,char *file)
{
	level_t level;
	make_level_table(&level,file);
	int i;
	for(i=0;i<EDIT_LIST_NUM;i++)
	{
		int j=0,k=0;
		while(j<level.level_num)
		{
			k=0;
			while(k<level.level_id[j].level_id_num)
			{
				if (MPU_sequence_number==level.level_id[j].level_sequence_num[k])
				{
					return level.level_id[j].level_id;
					break;
				}
				else
				k++;
			}
			j++;
		}
		
	}
	for(i=0;i<level.level_num;i++)
	{
		free(level.level_id[i].level_sequence_num);
		level.level_id[i].level_sequence_num=NULL;
	}
	free(level.level_id);
	level.level_id=NULL;
    return -1;
}

int make_mp_table(mp_table_t* mp_table, asset_info_t* asset_info, char * file, u_int32_t MPU_sequence_number)  //changed
{

    int count = 0;
//    mp_table_t mp_table;
    mp_table->table_id=0x20;        //complete MP table
    mp_table->version=0x00;
    mp_table->length=0;
    mp_table->reserved = 63; //all 1
    mp_table->MP_table_mode = 0x00;
    mp_table->number_of_assets=asset_info->assets_count;

    mp_table->MP_table_asset= (MP_table_asset_t *)malloc(sizeof(MP_table_asset_t )*(mp_table->number_of_assets));

    for(count=0; count<(mp_table->number_of_assets);count++)
    {
        mp_table->MP_table_asset[count].Identifier_mapping = (Identifier_mapping_t *)malloc(sizeof(Identifier_mapping_t));

        mp_table->MP_table_asset[count].Identifier_mapping->identifier_type=0x00;        //00 for asset_id
        mp_table->MP_table_asset[count].Identifier_mapping->identifier_mapping_byte = (Identifier_mapping_byte_t *)malloc(sizeof(Identifier_mapping_byte_t));
        mp_table->MP_table_asset[count].Identifier_mapping->identifier_mapping_byte->asset_id = (asset_id_t *)malloc(sizeof(asset_id_t));
        //asset_id_scheme is not correct now
        mp_table->MP_table_asset[count].Identifier_mapping->identifier_mapping_byte->asset_id->asset_id_scheme = 0;
        mp_table->MP_table_asset[count].Identifier_mapping->identifier_mapping_byte->asset_id->asset_id_length = strlen((asset_info+count)->asset_id);
        mp_table->MP_table_asset[count].Identifier_mapping->identifier_mapping_byte->asset_id->asset_id_byte = (unsigned char*)malloc(strlen((asset_info+count)->asset_id)*sizeof(char));
        memcpy(mp_table->MP_table_asset[count].Identifier_mapping->identifier_mapping_byte->asset_id->asset_id_byte,(asset_info+count)->asset_id,
                strlen((asset_info+count)->asset_id));

        char type_temp[4] = {*((asset_info+count)->asset_type),*((asset_info+count)->asset_type+1),*((asset_info+count)->asset_type+2),*((asset_info+count)->asset_type+3)};
        mp_table->MP_table_asset[count].asset_type = MP4_FOURCC(type_temp[1],type_temp[2],type_temp[3],type_temp[4]);

        mp_table->MP_table_asset[count].reserved = 0;
        mp_table->MP_table_asset[count].asset_clock_relation_flag= 0;

        mp_table->MP_table_asset[count].asset_loaction = (asset_location_t*)malloc(sizeof(asset_location_t));
        mp_table->MP_table_asset[count].asset_loaction->location_count = 1;
        //00 for packet_id
        mp_table->MP_table_asset[count].asset_loaction->general_location_info = (general_location_info_t *)malloc(sizeof(general_location_info_t));
        mp_table->MP_table_asset[count].asset_loaction->general_location_info->location_type = 0x00;
        mp_table->MP_table_asset[count].asset_loaction->general_location_info->general_location_info_byte = (general_location_info_byte_t *)malloc(sizeof(general_location_info_byte_t));
        mp_table->MP_table_asset[count].asset_loaction->general_location_info->general_location_info_byte->packet_id = (asset_info+count)->packet_id;

        //mp_table->MP_table_asset[count].asset_descriptors_length= 0;
        //mp_table->MP_table_asset[count].asset_descriptors_byte = NULL;
        MUR_descriptors_t mur_descriptor;
        int flag;
		if (file==NULL)
			flag=-2;
		else
			flag=mpu_in_json(MPU_sequence_number,file);
		int a;
		if (flag>-1)
		{
			mur_descriptor.descriptor_tag=0x00;
			mur_descriptor.descriptor_length=EDIT_LIST_NUM*sizeof(u_int8_t)+EDIT_LIST_NUM*sizeof(u_int32_t);
			mur_descriptor.edit_list=(edit_list_t *)malloc(EDIT_LIST_NUM*sizeof(edit_list_t));
			if (mur_descriptor.edit_list==NULL)
			{
				printf("error in malloc mur_descriptor.edit_list\n");
				exit(0);
			}
			mur_descriptor.edit_list->edit_list_id=flag;
			mur_descriptor.edit_list->mpu_sequence_number=MPU_sequence_number;

			mp_table->MP_table_asset[count].asset_descriptors_length= 4+mur_descriptor.descriptor_length;
			//printf("mp_table->MP_table_asset[count].asset_descriptors_length=%d\n",mp_table->MP_table_asset[count].asset_descriptors_length);
			unsigned char* mur_buf=(unsigned char *)malloc(mp_table->MP_table_asset[count].asset_descriptors_length*sizeof(char));
			if (mur_buf==NULL)
			{
				printf("error in malloc *mur_buf\n");
				exit(0);
			}
			*((u_int16_t*)&mur_buf[0])=htons(mur_descriptor.descriptor_tag);
			*((u_int16_t*)&mur_buf[2])=htons(mur_descriptor.descriptor_length);
			u_int32_t seekpoint=4;
			int i;
			for(i=0;i<EDIT_LIST_NUM;i++)
			{
				*((u_int8_t*)&mur_buf[seekpoint])=mur_descriptor.edit_list[i].edit_list_id;
				seekpoint+=1;
				*((u_int32_t*)&mur_buf[seekpoint])=htonl(mur_descriptor.edit_list[i].mpu_sequence_number);
				seekpoint+=sizeof(u_int32_t);
			}
			free(mur_descriptor.edit_list);
			mur_descriptor.edit_list=NULL;
			mp_table->MP_table_asset[count].asset_descriptors_byte = (unsigned char *)malloc(mp_table->MP_table_asset[count].asset_descriptors_length*sizeof(unsigned char));
			if (mp_table->MP_table_asset[count].asset_descriptors_byte==NULL)
			{
				printf("error in malloc mp_table->MP_table_asset[count].asset_descriptors_byte\n");
				exit(0);
			}
			memcpy(mp_table->MP_table_asset[count].asset_descriptors_byte,mur_buf,mp_table->MP_table_asset[count].asset_descriptors_length);
			free(mur_buf);
			mur_buf=NULL;
		}
		else
		{
			mp_table->MP_table_asset[count].asset_descriptors_byte=NULL;
			mp_table->MP_table_asset[count].asset_descriptors_length=0;
		}	

    }
//        mp_table.length=2+(4+1+6+2)*(mp_table.number_of_assets)+length_temp;
    mp_table->length = get_mp_table_length(mp_table)-4;

    return 0;
}

int read_MUR_descriptors(MUR_descriptors_t *mur_descriptor,const char *mur_buf)
{
	mur_descriptor->descriptor_tag=ntohs(*((u_int16_t*)&mur_buf[0]));
	mur_descriptor->descriptor_length=ntohs(*((u_int16_t*)&mur_buf[2]));
	u_int32_t seekpoint=4;
	//printf("mur_descriptor->descriptor_tag=%d\n",mur_descriptor->descriptor_tag);
	//printf("mur_descriptor->descriptor_length=%d\n",mur_descriptor->descriptor_length);
	mur_descriptor->edit_list=(edit_list_t *)malloc(EDIT_LIST_NUM*sizeof(edit_list_t));
	if (mur_descriptor->edit_list==NULL)
	{
		printf("error in malloc mur_descriptor->edit_list\n");
		exit(0);
	}
	int i;
	for(i=0;i<EDIT_LIST_NUM;i++)
	{
		mur_descriptor->edit_list[i].edit_list_id=*((u_int8_t*)&mur_buf[seekpoint]);
		seekpoint+=1;
		mur_descriptor->edit_list[i].mpu_sequence_number=ntohl(*((u_int32_t*)&mur_buf[seekpoint]));
		seekpoint+=sizeof(u_int32_t);
		//printf("mur_descriptor->edit_list[i].edit_list_id=%d\n",mur_descriptor->edit_list[i].edit_list_id);
		//printf("mur_descriptor->edit_list[i].mpu_sequence_number=%d\n",mur_descriptor->edit_list[i].mpu_sequence_number);
	}
	//free(mur_descriptor->edit_list);
	//mur_descriptor->edit_list=NULL;
	return 0;
}

int read_mp_table(mp_table_t *mp_table ,const char *mp_table_buf)
{

    mp_table->table_id=*((u_int8_t*)&mp_table_buf[0]);
    mp_table->version=*((u_int8_t*)&mp_table_buf[1]);
    mp_table->length=ntohs(*((u_int16_t*)&mp_table_buf[2]));
    mp_table->reserved = (*((u_int8_t*)&mp_table_buf[4]))>>2;
    mp_table->MP_table_mode = (*((u_int8_t*)&mp_table_buf[4]))&(0x03);
    mp_table->number_of_assets=*((u_int8_t*)&mp_table_buf[5]);
    mp_table->MP_table_asset= (MP_table_asset_t *)malloc(sizeof(MP_table_asset_t)*mp_table->number_of_assets);

    u_int32_t  i,seekpoint=6;
    u_int32_t j,location_num;
    for(i=0;i<mp_table->number_of_assets;i++)
    {
        mp_table->MP_table_asset[i].Identifier_mapping = (Identifier_mapping_t *)malloc(sizeof(Identifier_mapping_t));
        mp_table->MP_table_asset[i].Identifier_mapping->identifier_type = *((u_int8_t*)&mp_table_buf[seekpoint]);
        seekpoint += 1;
        mp_table->MP_table_asset[i].Identifier_mapping->identifier_mapping_byte = (Identifier_mapping_byte_t *)malloc(sizeof(Identifier_mapping_byte_t));

        //0x00 for asset_id
        if(mp_table->MP_table_asset[i].Identifier_mapping->identifier_type == 0x00)
        {
            u_int8_t length_tmp = 0;
            mp_table->MP_table_asset[i].Identifier_mapping->identifier_mapping_byte->asset_id = (asset_id_t *)malloc(sizeof(asset_id_t));
            mp_table->MP_table_asset[i].Identifier_mapping->identifier_mapping_byte->asset_id->asset_id_scheme = ntohs(*((u_int32_t *)&mp_table_buf[seekpoint]));
            length_tmp = *((u_int8_t *)&mp_table_buf[seekpoint+4]);

            seekpoint += 5;

            mp_table->MP_table_asset[i].Identifier_mapping->identifier_mapping_byte->asset_id->asset_id_length = length_tmp;
            //plus 1 for adding '\0' at the end
            mp_table->MP_table_asset[i].Identifier_mapping->identifier_mapping_byte->asset_id->asset_id_byte = (unsigned char*)malloc((length_tmp+1)*sizeof(char));
            memcpy(mp_table->MP_table_asset[i].Identifier_mapping->identifier_mapping_byte->asset_id->asset_id_byte,&mp_table_buf[seekpoint],length_tmp);
            mp_table->MP_table_asset[i].Identifier_mapping->identifier_mapping_byte->asset_id->asset_id_byte[length_tmp] = '\0';
            seekpoint += length_tmp;
        }
        else if(mp_table->MP_table_asset[i].Identifier_mapping->identifier_type == 0x01)
        {
            seekpoint += 0;
        }
        mp_table->MP_table_asset[i].asset_type = ntohs(*((u_int32_t *)&mp_table_buf[seekpoint]));
        mp_table->MP_table_asset[i].reserved = (*((u_int8_t*)&mp_table_buf[seekpoint+4]))>>1;
        mp_table->MP_table_asset[i].asset_clock_relation_flag = (*((u_int8_t*)&mp_table_buf[seekpoint+4]))&(0x01);

        mp_table->MP_table_asset[i].asset_loaction = (asset_location_t *)malloc(sizeof(asset_location_t));
        mp_table->MP_table_asset[i].asset_loaction->location_count = *((u_int8_t*)&mp_table_buf[seekpoint+5]);
        location_num = *((u_int8_t*)&mp_table_buf[seekpoint+5]);

        seekpoint += 6;

        mp_table->MP_table_asset[i].asset_loaction->general_location_info = (general_location_info_t *)malloc(sizeof(general_location_info_t)*location_num);
        for(j=0;j<location_num;j++)
        {
            mp_table->MP_table_asset[i].asset_loaction->general_location_info[j].location_type = *((u_int8_t*)&mp_table_buf[seekpoint]);
            seekpoint += 1;
            //0x00 for packet_id
            mp_table->MP_table_asset[i].asset_loaction->general_location_info[j].general_location_info_byte =
                    (general_location_info_byte_t *)malloc(sizeof(general_location_info_byte_t));

            if(mp_table->MP_table_asset[i].asset_loaction->general_location_info[j].location_type == 0x00)
            {
                mp_table->MP_table_asset[i].asset_loaction->general_location_info[j].general_location_info_byte->packet_id =
                        ntohs(*((u_int16_t *)&mp_table_buf[seekpoint]));
                seekpoint += 2;
            }
            //other location type
            else if(mp_table->MP_table_asset[i].asset_loaction->general_location_info[j].location_type == 0x01)
            {
                seekpoint += 0;
            }
        }

        mp_table->MP_table_asset[i].asset_descriptors_length = ntohs(*((u_int16_t *)&mp_table_buf[seekpoint]));
        mp_table->MP_table_asset[i].asset_descriptors_byte = (unsigned char *)malloc((mp_table->MP_table_asset[i].asset_descriptors_length+1)*sizeof(char));
        memcpy(mp_table->MP_table_asset[i].asset_descriptors_byte,&mp_table_buf[seekpoint+2],mp_table->MP_table_asset[i].asset_descriptors_length);
        //mp_table->MP_table_asset[i].asset_descriptors_byte[mp_table->MP_table_asset[i].asset_descriptors_length] = '\0';

        seekpoint += (2+mp_table->MP_table_asset[i].asset_descriptors_length);
    }

return 0;
}

int free_mp_table(mp_table_t *mp_table )
{
    u_int32_t  i, location_num, j;
    for (i=0;i<mp_table->number_of_assets;i++)
        {
//            free(mp_table->MP_table_asset[i].Identifier_mapping.URL_byte);
//            free(mp_table->MP_table_asset[i].asset_descriptors_byte);
            if(mp_table->MP_table_asset[i].Identifier_mapping->identifier_type == 0x00)
            {
                free(mp_table->MP_table_asset[i].Identifier_mapping->identifier_mapping_byte->asset_id->asset_id_byte);
                mp_table->MP_table_asset[i].Identifier_mapping->identifier_mapping_byte->asset_id->asset_id_byte = NULL;
            }
            free(mp_table->MP_table_asset[i].Identifier_mapping->identifier_mapping_byte);
            mp_table->MP_table_asset[i].Identifier_mapping->identifier_mapping_byte = NULL;
            free(mp_table->MP_table_asset[i].Identifier_mapping);
            mp_table->MP_table_asset[i].Identifier_mapping = NULL;

            location_num = mp_table->MP_table_asset[i].asset_loaction->location_count;
            for(j=0;j<location_num;j++)
            {
                free(mp_table->MP_table_asset[i].asset_loaction->general_location_info[j].general_location_info_byte);
                mp_table->MP_table_asset[i].asset_loaction->general_location_info[j].general_location_info_byte = NULL;
            //    free(&(mp_table->MP_table_asset[i].asset_loaction->general_location_info[j]));
            }
            free(mp_table->MP_table_asset[i].asset_loaction->general_location_info);
            mp_table->MP_table_asset[i].asset_loaction->general_location_info = NULL;
            free(mp_table->MP_table_asset[i].asset_loaction);
            mp_table->MP_table_asset[i].asset_loaction = NULL;
            free(mp_table->MP_table_asset[i].asset_descriptors_byte);
            mp_table->MP_table_asset[i].asset_descriptors_byte = NULL;
        }
    free(mp_table->MP_table_asset);
    mp_table->MP_table_asset = NULL;
    return 0;

}


int init_mpi_table(mpi_table_t *mpi_table, unsigned char **mpi_table_buf)
{
    unsigned char *mpi_table_buf_tmp= (unsigned   char*) malloc((4+mpi_table->length)*sizeof( unsigned   char));
    if(mpi_table_buf_tmp==NULL)
    {
        puts ("Memory allocation failed.");
        exit (EXIT_FAILURE);
    }

    *((u_int8_t*)&mpi_table_buf_tmp[0])=mpi_table->table_id;
    *((u_int8_t*)&mpi_table_buf_tmp[1])=mpi_table->version;
    *((u_int16_t*)&mpi_table_buf_tmp[2])=htons(mpi_table->length);

    *((u_int8_t*)&mpi_table_buf_tmp[4])=((mpi_table->reserved1<<4)|(mpi_table->PI_mode<<2)|(mpi_table->reserved2));

    *((u_int16_t*)&mpi_table_buf_tmp[5]) = htons(mpi_table->MPIT_descriptor->descriptors_length);
    u_int32_t  i,seekpoint=7;

    if(mpi_table->MPIT_descriptor->descriptors_length != 0)
    {
        memcpy(&mpi_table_buf_tmp[seekpoint],mpi_table->MPIT_descriptor->MPIT_descriptors_byte,mpi_table->MPIT_descriptor->descriptors_length);
    }
    //    PRINTF("seekpoint%d\n",seekpoint);
    //    PRINTF("seek%s\n",mpi_table_buf_tmp);
    //    PRINTF("seek%c\n",mpi_table_buf_tmp[8]);

    seekpoint += mpi_table->MPIT_descriptor->descriptors_length;

    *((u_int8_t*)&mpi_table_buf_tmp[seekpoint]) = mpi_table->PI_content_count;
    seekpoint += 1;

    for (i=0;i<mpi_table->PI_content_count;i++)
    {
        *((u_int8_t*)&mpi_table_buf_tmp[seekpoint]) = mpi_table->PI_content[i].PI_content_type_length;
        memcpy(&mpi_table_buf_tmp[seekpoint+1],mpi_table->PI_content[i].PI_content_type_byte,mpi_table->PI_content[i].PI_content_type_length);
        seekpoint += 1+mpi_table->PI_content[i].PI_content_type_length;

        *((u_int8_t*)&mpi_table_buf_tmp[seekpoint])=mpi_table->PI_content[i].PI_content_name_length;
        memcpy(&mpi_table_buf_tmp[seekpoint+1] , mpi_table->PI_content[i].PI_content_name_byte , mpi_table->PI_content[i].PI_content_name_length);
        seekpoint += 1+mpi_table->PI_content[i].PI_content_name_length;

        *((u_int16_t*)&mpi_table_buf_tmp[seekpoint])=htons(mpi_table->PI_content[i].PI_content_descriptors_length);
        if(mpi_table->PI_content[i].PI_content_descriptors_length != 0)
        {
            memcpy(&mpi_table_buf_tmp[seekpoint+2] , mpi_table->PI_content[i].PI_content_descriptors_byte , mpi_table->PI_content[i].PI_content_descriptors_length);
        }
        seekpoint += 2+mpi_table->PI_content[i].PI_content_descriptors_length;

        *((u_int16_t*)&mpi_table_buf_tmp[seekpoint])=htons(mpi_table->PI_content[i].PI_content_length);
        memcpy(&mpi_table_buf_tmp[seekpoint+2] , mpi_table->PI_content[i].PI_content_byte , mpi_table->PI_content[i].PI_content_length);
        seekpoint += 2+mpi_table->PI_content[i].PI_content_length;
    }
    *mpi_table_buf=mpi_table_buf_tmp;
    return 0;
}

int make_mpi_table(mpi_table_t* mpi_table, PI_info_t* PI_info)
{
    mpi_table->table_id=0x10;
    mpi_table->version=0;
    mpi_table->length=0;
    mpi_table->reserved1=0;
    mpi_table->PI_mode=0;
    mpi_table->reserved2=0;

    //MPI table descriptors is not used now
    mpi_table->MPIT_descriptor = (MPIT_descriptors_t *)malloc(sizeof(MPIT_descriptors_t));
    mpi_table->MPIT_descriptor->descriptors_length=0;
    mpi_table->MPIT_descriptor->MPIT_descriptors_byte=NULL;
    mpi_table->PI_content_count=PI_info->PI_cotent_count;
    mpi_table->PI_content= (PI_content_t *)malloc(sizeof(PI_content_t )*mpi_table->PI_content_count);
        if(mpi_table->PI_content==NULL)
        {
            puts ("Memory allocation failed.");
             exit (EXIT_FAILURE);
        }
        int pi_count=0, pi_length_tmp=0;

        for(pi_count=0;pi_count<mpi_table->PI_content_count;pi_count++)
        {
            mpi_table->PI_content[pi_count].PI_content_type_length = strlen(PI_info[pi_count].PI_content_type) + 1;
            mpi_table->PI_content[pi_count].PI_content_type_byte = (unsigned char*)malloc((mpi_table->PI_content[pi_count].PI_content_type_length)*sizeof(unsigned char));
            memcpy(mpi_table->PI_content[pi_count].PI_content_type_byte,PI_info[pi_count].PI_content_type,mpi_table->PI_content[pi_count].PI_content_type_length);
			mpi_table->PI_content[pi_count].PI_content_type_byte[mpi_table->PI_content[pi_count].PI_content_type_length] = '\0';

            mpi_table->PI_content[pi_count].PI_content_name_length=strlen(PI_info[pi_count].PI_content_name) + 1;
            mpi_table->PI_content[pi_count].PI_content_name_byte = (unsigned char*)malloc(mpi_table->PI_content[pi_count].PI_content_name_length*sizeof(unsigned char));
            memcpy(mpi_table->PI_content[pi_count].PI_content_name_byte,PI_info[pi_count].PI_content_name,mpi_table->PI_content[pi_count].PI_content_name_length);
			mpi_table->PI_content[pi_count].PI_content_name_byte[mpi_table->PI_content[pi_count].PI_content_name_length] = '\0';


            mpi_table->PI_content[pi_count].PI_content_descriptors_length = 0;
            mpi_table->PI_content[pi_count].PI_content_descriptors_byte = NULL;

            int readFlag = ReadFile(strcatex(PI_info[pi_count].PI_content_path,PI_info[pi_count].PI_content_name),(char **)&(mpi_table->PI_content[pi_count].PI_content_byte),
                        &mpi_table->PI_content[pi_count].PI_content_length);
            if(readFlag == -1)
            {
                //PRINTF("can't find ci or html\n");
                exit(0);
            }

            pi_length_tmp += mpi_table->PI_content[pi_count].PI_content_type_length+mpi_table->PI_content[pi_count].PI_content_name_length+ \
                    mpi_table->PI_content[pi_count].PI_content_descriptors_length+mpi_table->PI_content[pi_count].PI_content_length;
        }

        mpi_table->length = 1+2+mpi_table->MPIT_descriptor->descriptors_length+1+6*mpi_table->PI_content_count+pi_length_tmp;
        return 0;
}

int read_mpi_table(mpi_table_t *mpi_table, const char *mpi_table_buf)
{
        mpi_table->table_id=*((u_int8_t*)&mpi_table_buf[0]);
        mpi_table->version=*((u_int8_t*)&mpi_table_buf[1]);
        mpi_table->length=ntohs(*((u_int16_t*)&mpi_table_buf[2]));

        mpi_table->reserved1 = ((*((u_int8_t*)&mpi_table_buf[4]))>>4)&(0x0F);
        mpi_table->PI_mode = ((*((u_int8_t*)&mpi_table_buf[4]))>>2)&(0x03);
        mpi_table->reserved2 = (*((u_int8_t*)&mpi_table_buf[4]))&(0x03);

        mpi_table->MPIT_descriptor = (MPIT_descriptors_t *)malloc(sizeof(MPIT_descriptors_t));
        mpi_table->MPIT_descriptor->descriptors_length = ntohs(*((u_int16_t*)&mpi_table_buf[5]));

        u_int32_t  i,seekpoint=7;
        if(mpi_table->MPIT_descriptor->descriptors_length != 0)
        {
            mpi_table->MPIT_descriptor->MPIT_descriptors_byte = (unsigned char*)malloc((mpi_table->MPIT_descriptor->descriptors_length+1)*sizeof(unsigned char));
            memcpy(mpi_table->MPIT_descriptor->MPIT_descriptors_byte,&mpi_table_buf[seekpoint],mpi_table->MPIT_descriptor->descriptors_length);
            mpi_table->MPIT_descriptor->MPIT_descriptors_byte[mpi_table->MPIT_descriptor->descriptors_length] = '\0';
        }
        seekpoint += mpi_table->MPIT_descriptor->descriptors_length;

        mpi_table->PI_content_count=*((u_int8_t*)&mpi_table_buf[seekpoint]);
        mpi_table->PI_content= (PI_content_t *)malloc(sizeof(PI_content_t )*mpi_table->PI_content_count);
        seekpoint += 1;

        for (i=0;i<mpi_table->PI_content_count;i++)
        {
            mpi_table->PI_content[i].PI_content_type_length = *((u_int8_t*)&mpi_table_buf[seekpoint]);
            mpi_table->PI_content[i].PI_content_type_byte = (unsigned char*)malloc((mpi_table->PI_content[i].PI_content_type_length+1)*sizeof(unsigned char));
            memcpy(mpi_table->PI_content[i].PI_content_type_byte,&mpi_table_buf[seekpoint+1],mpi_table->PI_content[i].PI_content_type_length);
            mpi_table->PI_content[i].PI_content_type_byte[mpi_table->PI_content[i].PI_content_type_length] = '\0';
            seekpoint += 1+mpi_table->PI_content[i].PI_content_type_length;

            mpi_table->PI_content[i].PI_content_name_length = *((u_int8_t*)&mpi_table_buf[seekpoint]);
            mpi_table->PI_content[i].PI_content_name_byte = (unsigned  char*) malloc((mpi_table->PI_content[i].PI_content_name_length+1)*sizeof( unsigned  char));
            memcpy(mpi_table->PI_content[i].PI_content_name_byte , &mpi_table_buf[seekpoint+1] , mpi_table->PI_content[i].PI_content_name_length);
            mpi_table->PI_content[i].PI_content_name_byte[mpi_table->PI_content[i].PI_content_name_length] = '\0';
            seekpoint += 1+mpi_table->PI_content[i].PI_content_name_length;

            mpi_table->PI_content[i].PI_content_descriptors_length = ntohs(*((u_int16_t*)&mpi_table_buf[seekpoint]));
            if(mpi_table->PI_content[i].PI_content_descriptors_length != 0)
            {
                mpi_table->PI_content[i].PI_content_descriptors_byte = (unsigned  char*) malloc((mpi_table->PI_content[i].PI_content_descriptors_length+1)*sizeof( unsigned  char));
                memcpy(mpi_table->PI_content[i].PI_content_descriptors_byte , &mpi_table_buf[seekpoint+1] , mpi_table->PI_content[i].PI_content_descriptors_length);
                mpi_table->PI_content[i].PI_content_descriptors_byte[mpi_table->PI_content[i].PI_content_descriptors_length] = '\0';
            }
            seekpoint += 2+mpi_table->PI_content[i].PI_content_descriptors_length;

            mpi_table->PI_content[i].PI_content_length = ntohs(*((u_int16_t*)&mpi_table_buf[seekpoint]));
            mpi_table->PI_content[i].PI_content_byte=(unsigned  char*) malloc((mpi_table->PI_content[i].PI_content_length+1)*sizeof( unsigned  char));
            memcpy(mpi_table->PI_content[i].PI_content_byte ,&mpi_table_buf[seekpoint+2], mpi_table->PI_content[i].PI_content_length);
            mpi_table->PI_content[i].PI_content_byte[mpi_table->PI_content[i].PI_content_length] = '\0';
            seekpoint += 2+mpi_table->PI_content[i].PI_content_length;
#if 0
            FILE *mpi;
            if((mpi=fopen((const char*)mpi_table->PI_content[i].PI_content_name_byte,"w+"))==NULL)
                    {
                        printf("not open");
                        exit(0);
                    }
            fwrite(mpi_table->PI_content[i].PI_content_byte,mpi_table->PI_content[i].PI_content_length,1,mpi);
            fclose(mpi);
#endif
        }
     return 0;

}

int copy_mpi_table(mpi_table_t *mpi_table_dst, mpi_table_t  *mpi_table_src)
{
        mpi_table_dst->table_id=mpi_table_src->table_id;
        mpi_table_dst->version=mpi_table_src->version;
        mpi_table_dst->length=mpi_table_src->length;

        mpi_table_dst->PI_mode=mpi_table_src->PI_mode;
        //temp var
        mpi_table_dst->PI_content_count=mpi_table_src->PI_content_count;
        mpi_table_dst->PI_content= (PI_content_t *)malloc(sizeof(PI_content_t )*mpi_table_src->PI_content_count);
        u_int32_t  i;

        for (i=0;i<mpi_table_src->PI_content_count;i++)
        {
            //*((u_int8_t*)&mpi_table_buf[seekpoint])=(u_int8_t)(i+1);
            mpi_table_dst->PI_content[i].PI_content_name_length=mpi_table_src->PI_content[i].PI_content_name_length;
            mpi_table_dst->PI_content[i].PI_content_name_byte=(unsigned  char*) malloc((mpi_table_src->PI_content[i].PI_content_name_length+1)*sizeof( unsigned  char));
            memset(mpi_table_dst->PI_content[i].PI_content_name_byte,0,(mpi_table_src->PI_content[i].PI_content_name_length+1));
            memcpy(mpi_table_dst->PI_content[i].PI_content_name_byte , mpi_table_src->PI_content[i].PI_content_name_byte , mpi_table_src->PI_content[i].PI_content_name_length);
            mpi_table_dst->PI_content[i].PI_content_length=mpi_table_src->PI_content[i].PI_content_length;
            mpi_table_dst->PI_content[i].PI_content_byte=(unsigned  char*) malloc((mpi_table_src->PI_content[i].PI_content_length+1)*sizeof( unsigned  char));
            memset(mpi_table_dst->PI_content[i].PI_content_byte,0,(mpi_table_dst->PI_content[i].PI_content_length+1));
            memcpy( mpi_table_dst->PI_content[i].PI_content_byte ,mpi_table_src->PI_content[i].PI_content_byte , mpi_table_src->PI_content[i].PI_content_length);

        }
     return 0;

}

int free_mpi_table(mpi_table_t *mpi_table)
{
    u_int32_t  i;
//
    if(mpi_table->MPIT_descriptor->MPIT_descriptors_byte!=NULL)
    {
        free(mpi_table->MPIT_descriptor->MPIT_descriptors_byte);
        mpi_table->MPIT_descriptor->MPIT_descriptors_byte =  NULL;
    }
    free(mpi_table->MPIT_descriptor);
    mpi_table->MPIT_descriptor = NULL;

    for (i=0;i<mpi_table->PI_content_count;i++)
    {
        free(mpi_table->PI_content[i].PI_content_type_byte);
        mpi_table->PI_content[i].PI_content_type_byte = NULL;
        free(mpi_table->PI_content[i].PI_content_name_byte);
        mpi_table->PI_content[i].PI_content_name_byte = NULL;
        if(mpi_table->PI_content[i].PI_content_descriptors_byte != NULL)
        {
            free(mpi_table->PI_content[i].PI_content_descriptors_byte);
            mpi_table->PI_content[i].PI_content_descriptors_byte = NULL;
        }
        if(!mpi_table->PI_content[i].PI_content_byte) free(mpi_table->PI_content[i].PI_content_byte);
        mpi_table->PI_content[i].PI_content_byte = NULL;
    }
    free(mpi_table->PI_content);
    mpi_table->PI_content = NULL;

    return 0;
}
int send_signal(URLContext *h, pa_message_t *pa_header,unsigned char *pa_message_buf,u_int32_t *packet_sequence_number,u_int32_t *packet_counter,u_int32_t packet_id);
int send_signal(URLContext *h, pa_message_t *pa_header,unsigned char *pa_message_buf,u_int32_t *packet_sequence_number,u_int32_t *packet_counter,u_int32_t packet_id)
{
    int pa_sequence_number=(pa_header->length+4+MMTP_BUFF_LEN-1)/(unsigned int)MMTP_BUFF_LEN;;
    int pa_length=pa_header->length+PAh_BUFF_LEN;
    u_int32_t buf_seekpoint=0;
    smt_status status;

    int counter;
    for (counter=0;counter<pa_sequence_number;counter++)
    {
        signal_header_t signal_header;
        if(counter==(pa_sequence_number-1))
        {
            signal_header.MSG_length1=pa_length%signal_BUFF_LEN;
            //有问题
            if(pa_length%MMTP_BUFF_LEN==0)
            {
                signal_header.MSG_length1=signal_BUFF_LEN;
            }
        }
        else
        {
            signal_header.MSG_length1=signal_BUFF_LEN;
        }
        signal_header.f_i=0;
        signal_header.res=0;
        signal_header.H=0;
        signal_header.A=0;
        signal_header.frag_counter=0;

        if(pa_sequence_number>1)
        {    signal_header.frag_counter=pa_sequence_number;
            if(counter==0)
                signal_header.f_i=1;
            else if(counter==(pa_sequence_number-1))
            {
                signal_header.f_i=3;
            }
            else
            {
                signal_header.f_i=2;
            }
        }
        else
        {
            signal_header.f_i=0;
            signal_header.frag_counter=1;
        }
        signal_header.A=0;

        unsigned char Signal_h[Signal_h_BUFF_LEN]={};

        init_signal_header(&signal_header,Signal_h);

        // printf("!!!!!!!!!!!!!\n");
        // int i = 0;
        // for(i = 0; i < Signal_h_BUFF_LEN; ++i){
        //     printf("%u    ",Signal_h[i]);
        // }
        // printf("\n!!!!!!!!!!!!!\n");
        //16byte
        mmt_packet_header_t mmt_header;
        unsigned char MMTPh[MMTPh_BUFF_LEN];
        mmt_header.version=0;
        mmt_header.packet_counter_flag=1;
        mmt_header.FEC_type=0;
        mmt_header.reserved_1=0;
        mmt_header.extension_flag=0;
        mmt_header.RAP_flag=0;
        mmt_header.reserved_2=0;
        mmt_header.type=2;
        mmt_header.packet_id=packet_id;
        mmt_header.timestamp=get_send_timestamp();
        mmt_header.packet_sequence_number=*packet_sequence_number;
        mmt_header.packet_counter=(*packet_counter);

        init_mmtp_header(&mmt_header,MMTPh);

        char UDPbuff[UDP_BUFF_LEN]={};

        memcpy(UDPbuff,MMTPh,MMTPh_BUFF_LEN);
        //diyige jia tou
        if (signal_header.f_i==1)
        {
            memcpy(&UDPbuff[MMTPh_BUFF_LEN],Signal_h,Signal_h_BUFF_LEN);
            memcpy(&UDPbuff[MMTPh_BUFF_LEN+Signal_h_BUFF_LEN],&pa_message_buf[buf_seekpoint],signal_header.MSG_length1);
        }
        else
        {
            memcpy(&UDPbuff[MMTPh_BUFF_LEN],Signal_h,Signal_h_BUFF_LEN);
            memcpy(&UDPbuff[MMTPh_BUFF_LEN+Signal_h_BUFF_LEN],&pa_message_buf[buf_seekpoint],signal_header.MSG_length1);

        }
       
    
        // printf("=============================================\n");
        // printf("data=\n");
        // int i = 0;
        // for(i  = 0; i < signal_header.MSG_length1; ++i){
        //     printf("%u",pa_message_buf[buf_seekpoint+i]);
        // }
        // printf("\n");
        // printf("MSG_length1=%d\n",signal_header.MSG_length1);
        // printf("=============================================\n");
        // fflush(stdout);
         buf_seekpoint=buf_seekpoint+signal_header.MSG_length1;
        //send data
        if(0 > smt_callback_entity.packet_send(h, UDPbuff, UDP_BUFF_LEN)){
            av_log(h, AV_LOG_ERROR, "send smt signal packet failed.\n");
            status = SMT_STATUS_ERROR;
            break;
        }
        if((*packet_counter)==maximum_value)
        {
            *packet_counter=0;
        }
        else
        {
            (*packet_counter)++;
        }
        if((*packet_sequence_number)==maximum_value)
        {
            *packet_sequence_number=0;
        }
        else
        {
            (*packet_sequence_number)++;
        }
    }
    PRINTF("finished send signal\n");
    return 0;

}

void generate_and_send_pa_message(URLContext *h, 
                              asset_info_t    *asset_info, 
                              PI_info_t       *PI_info,
                              u_int32_t       *p_packet_counter);
void generate_and_send_pa_message(URLContext *h, 
                              asset_info_t    *asset_info, 
                              PI_info_t       *PI_info,
                              u_int32_t       *p_packet_counter) {
    u_int32_t packet_sequence_number_signal=0;
    //PA table
    pa_table_t pa_table ;
    unsigned char pa_table_buf[1024];
    pa_table.table_id=0x00;
    pa_table.version=0;
    pa_table.length=0;
    pa_table.pat_content=NULL;
    init_pa_table(&pa_table,pa_table_buf);

    //MP table
    //changed-------------
    mp_table_t mp_table;
    char * file  = NULL;
    int mpu_sequence_number = 0;
    make_mp_table(&mp_table,asset_info,file,mpu_sequence_number);
    //-------------------
    unsigned char *mp_table_buf;
    mp_table_buf= (unsigned  char*) malloc((4+mp_table.length)*sizeof( unsigned  char));
    if(mp_table_buf==NULL)
    {
        puts ("Memory allocation failed.");
        exit (EXIT_FAILURE);
    }

    init_mp_table(&mp_table,&mp_table_buf);

    free_mp_table(&mp_table);
    //MPI table
    mpi_table_t mpi_table;

    make_mpi_table(&mpi_table, PI_info);
    unsigned char *mpi_table_buf = NULL;
    mpi_table_buf= (unsigned  char*) malloc((4+mpi_table.length)*sizeof( unsigned  char));
    if(mpi_table_buf==NULL)
    {
        puts ("Memory allocation failed.");
        exit (EXIT_FAILURE);
    }
    mpi_table_t *p_mpi_table = &mpi_table;
    unsigned char *p_mpi_table_buf = &mpi_table_buf;
    //init_mpi_table(&mpi_table,&mpi_table_buf);
    init_mpi_table(p_mpi_table,p_mpi_table_buf);

    free_mpi_table(&mpi_table);

    //PA message

    pa_message_t pa_header ;
    unsigned char PAh[8];
    pa_header.message_id=0x0000;
    pa_header.version=0;
    pa_header.length=0;
    pa_header.number_of_tables=3;

    //*2 because of include each table header twice
    pa_header.length=pa_table.length+mp_table.length+mpi_table.length+table_header_LEN*pa_header.number_of_tables*2+1;


    init_pa_message(&pa_header,PAh);

    unsigned char *pa_message_buf= (unsigned  char*) malloc((7+pa_header.length)*sizeof( unsigned  char));
    if(pa_message_buf==NULL)  {
        puts ("Memory allocation failed.");
        exit (EXIT_FAILURE);
    }
    u_int32_t pa_seekpoint=0;
    memcpy(&pa_message_buf[pa_seekpoint] , PAh , 8);
    pa_seekpoint=pa_seekpoint+8;
    memcpy(&pa_message_buf[pa_seekpoint] , mp_table_buf , 4);
    pa_seekpoint=pa_seekpoint+4;
    memcpy(&pa_message_buf[pa_seekpoint] , mpi_table_buf , 4);
    pa_seekpoint=pa_seekpoint+4;
    memcpy(&pa_message_buf[pa_seekpoint] , pa_table_buf , 4);
    pa_seekpoint=pa_seekpoint+4;
    memcpy(&pa_message_buf[pa_seekpoint] , mp_table_buf , mp_table.length+4);
    pa_seekpoint=pa_seekpoint+mp_table.length+4;
    memcpy(&pa_message_buf[pa_seekpoint] , mpi_table_buf , mpi_table.length+4);
    pa_seekpoint=pa_seekpoint+mpi_table.length+4;
    memcpy(&pa_message_buf[pa_seekpoint] , pa_table_buf , pa_table.length+4);
    pa_seekpoint=pa_seekpoint+pa_table.length+4;

    send_signal(h,
                &pa_header,
                pa_message_buf,
                &packet_sequence_number_signal,
                p_packet_counter,
                Signal_PACKET_ID);
    if(NULL != mp_table_buf) free(mp_table_buf);
}

asset_info_t    asset_info_base[2]= {
    {
		0xffff,     //u_int16_t last_time;
		2,          //u_int8_t assets_count;
        "asset0",   //char *asset_id;
        "video",    //char *media_type;
        "video",    //char *asset_type;
        0,          //uint16_t packet_id;
        " ",        //char *asset_path;
        " ",        //char *asset_send_begintime;
        " ",        //char *asset_send_endtime;
    },
    {
		0xffff,     //u_int16_t last_time;
		2,          //u_int8_t assets_count;
        "asset1",   //char *asset_id;
        "audio",    //char *media_type;
        "audio",    //char *asset_type;
        1,          //uint16_t packet_id;
        " ",        //char *asset_path;
        " ",        //char *asset_send_begintime;
        " ",        //char *asset_send_endtime;
    },
};
PI_info_t       PI_info_base[2] = {
    {
        2,              //u_int8_t PI_cotent_count;
        "text/xml",     //char *PI_content_type;
        "ci",           //char *PI_type;
        "ci.xml",       //char *PI_content_name;
        "./",           //char *PI_content_path;
    },
    {
        2,              //u_int8_t PI_cotent_count;
        "text/html",    //char *PI_content_type;
        "html",         //char *PI_type;
        "index.html",   //char *PI_content_name;
        "./",           //char *PI_content_path;
    }  
};

int parsexml(char* file, asset_info_t* asset_info_base, PI_info_t* PI_info_base)
{
        xmlDocPtr doc;
        xmlNodePtr curNode;
        xmlChar *szKey;
        char *szDocName;
        szDocName = file;
        doc = xmlReadFile(file,"GB2312",XML_PARSE_RECOVER);
	if (NULL == doc)
	{
		fprintf(stderr, "Document not parsed successfully.\n");
		return -1;
	}
	curNode = xmlDocGetRootElement(doc);
	if (xmlStrcmp(curNode->name, BAD_CAST "Signaling"))
	{
		fprintf(stderr, "document of the wrong type, Sinaling node != Signaling");
		xmlFreeDoc(doc);
		return -1;
	}
	curNode = curNode->children;
	curNode = curNode->next;
    	curNode = curNode->children;
    	curNode = curNode->next;
    	xmlNodePtr propNodePtr1 = curNode; //切换到asset结点
    	int i = 0;
	while (propNodePtr1 != NULL)
	{
		//xmlAttrPtr attrPtr1 = propNodePtr1->properties;
	        asset_info_base[i].asset_id = xmlGetProp(propNodePtr1, BAD_CAST "asset_id");
       	        asset_info_base[i].media_type = xmlGetProp(propNodePtr1, BAD_CAST "media_type");
                asset_info_base[i].asset_type = xmlGetProp(propNodePtr1, BAD_CAST "asset_type");
	        asset_info_base[i].packet_id = (u_int16_t)*xmlGetProp(propNodePtr1, BAD_CAST "packet_id")-48;
		propNodePtr1 = propNodePtr1->next; //text结点
		propNodePtr1 = propNodePtr1->next;
		i = i+1;
	}
	curNode = curNode->parent;
        curNode = curNode->next;
        curNode = curNode->next;
        curNode = curNode->children;
        curNode = curNode->next;
        xmlNodePtr propNodePtr2 = curNode; //切换到PI结点
    i=0;
	while (propNodePtr2 != NULL)
	{
		//xmlAttrPtr attrPtr2 = propNodePtr2->properties;
		PI_info_base[i].PI_type = xmlGetProp(propNodePtr2, BAD_CAST "PI_type");
		PI_info_base[i].PI_content_name = xmlGetProp(propNodePtr2, BAD_CAST "PI_content_name");
		PI_info_base[i].PI_content_path = xmlGetProp(propNodePtr2, BAD_CAST "PI_content_path");
		propNodePtr2 = propNodePtr2->next; //text结点
		propNodePtr2 = propNodePtr2->next;
		i = i+1;
	}
	xmlFreeDoc(doc);
	return 0;
}

void generate_and_send_signal(URLContext *h)
{
    parsexml("/home/frank/work/smt/smt/smt-player/ffmpeg/Signaling.xml", asset_info_base, PI_info_base);
    u_int32_t       packet_counter;
    int i;
    asset_info_t *asset_info = NULL;
    PI_info_t    *PI_info = NULL;
    asset_info  = (asset_info_t*) malloc(2 * sizeof(asset_info_t));
    memcpy(asset_info, asset_info_base, 2 * sizeof(asset_info_t));
    PI_info = (PI_info_t*) malloc(2 * sizeof(PI_info_t));
    memcpy(PI_info, PI_info_base, 2* sizeof(PI_info_t));
    for(i = 0; i < 2; i++) 
    {
        asset_info[i].asset_id = (char*) malloc(strlen(asset_info_base[i].asset_id) + 1);
        strcpy(asset_info[i].asset_id, asset_info_base[i].asset_id);
        asset_info[i].media_type = (char*) malloc(strlen(asset_info_base[i].media_type) + 1);
        strcpy(asset_info[i].media_type, asset_info_base[i].media_type);
        asset_info[i].asset_type = (char*) malloc(strlen(asset_info_base[i].asset_type) + 1);
        strcpy(asset_info[i].asset_type, asset_info_base[i].asset_type);
        asset_info[i].asset_send_begintime= (char*) malloc(strlen(asset_info_base[i].asset_send_begintime) + 1);
        strcpy(asset_info[i].asset_send_begintime, asset_info_base[i].asset_send_begintime);
        asset_info[i].asset_send_endtime= (char*) malloc(strlen(asset_info_base[i].asset_send_endtime) + 1);
        strcpy(asset_info[i].asset_send_endtime, asset_info_base[i].asset_send_endtime);
    }
    for(i = 0; i < 2; i++) 
    {
        PI_info[i].PI_content_type = (char*)malloc(strlen(PI_info_base[i].PI_content_type) + 1);
        strcpy(PI_info[i].PI_content_type, PI_info_base[i].PI_content_type);
        PI_info[i].PI_content_type[strlen(PI_info_base[i].PI_content_type) + 1] = '\0';
        PI_info[i].PI_type = (char*)malloc(strlen(PI_info_base[i].PI_type) + 1);
        strcpy(PI_info[i].PI_type, PI_info_base[i].PI_type);
        PI_info[i].PI_content_name = (char*)malloc(strlen(PI_info_base[i].PI_content_name) + 1);
        strcpy(PI_info[i].PI_content_name, PI_info_base[i].PI_content_name);
        PI_info[i].PI_content_path = (char*)malloc(strlen(PI_info_base[i].PI_content_path) + 1);
        strcpy(PI_info[i].PI_content_path, PI_info_base[i].PI_content_path);
    }
    generate_and_send_pa_message(h, 
                              asset_info, 
                              PI_info,
                              &packet_counter);
}

int init_signal_header(signal_header_t *signal_header,unsigned char *Signal_h)

{
    *((u_int8_t*)&Signal_h[0])=(u_int8_t)signal_header->f_i<<6
                                |(u_int8_t) signal_header->res<<2
                                |(u_int8_t) signal_header->H<<1
                                |(u_int8_t) signal_header->A;
    *((u_int8_t*)&Signal_h[1])=signal_header->frag_counter;
    *((u_int16_t*)&Signal_h[2])=htons(signal_header->MSG_length1);

    // printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
    // printf("f_i=%u\nres=%u\nH=%u\nA=%u\ncounter=%u\nlength=%u\n",
    //     signal_header->f_i,signal_header->res,signal_header->H,signal_header->A,signal_header->frag_counter,signal_header->MSG_length1);
    // printf("~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n");
    return 0;

}


int get_send_timestamp()
{
    int MMT_timestamp;
    struct timeval tv;
    gettimeofday(&tv, NULL);

    time_t timep;
    struct tm *p;
    time(&timep);
    p = localtime(&timep);

    MMT_timestamp=p->tm_hour*60*60*1000+p->tm_min*60*1000+p->tm_sec*1000+tv.tv_usec/1000;
    return MMT_timestamp;
}
int init_mmtp_header(mmt_packet_header_t *mmt_header,unsigned char *MMTPh)
{
    memset(MMTPh,0,MMTPh_BUFF_LEN);
    MMTPh[0]=(u_int8_t)mmt_header->version<<6
        |(u_int8_t) mmt_header->packet_counter_flag<<5
        |(u_int8_t) mmt_header->FEC_type<<3
        |(u_int8_t) mmt_header->reserved_1<<2
        |(u_int8_t) mmt_header->extension_flag<<1
        |(u_int8_t) mmt_header->RAP_flag;
    MMTPh[1]=(u_int8_t)mmt_header->reserved_2<<6
        |(u_int8_t) mmt_header->type;
    *((u_int16_t*)&MMTPh[2])=htons(mmt_header->packet_id);
    *((u_int32_t*)&MMTPh[4])=htonl(mmt_header->timestamp);
    *((u_int32_t*)&MMTPh[8])=htonl(mmt_header->packet_sequence_number);
    *((u_int32_t*)&MMTPh[12])=htonl(mmt_header->packet_counter);

    return 0;
}

int get_mp_table_length(mp_table_t *mp_table)
{
    int i = 0;
    //table_length here equals table.length +1+1+2
    int table_length = 0,mp_table_asset_len = 0;
    //+table_id+version+length+reserved+MP_table_mode
    table_length = 1+1+2+1+1;

    int asset_num = mp_table->number_of_assets;

    for(i=0;i<asset_num;i++)
    {
        int j = 0;
        int location_num = mp_table->MP_table_asset[i].asset_loaction->location_count;
        int identifier_mapping_length =0 ;
        //0x00 for asset_id
        if(mp_table->MP_table_asset[i].Identifier_mapping->identifier_type == 0x00)
        {
            identifier_mapping_length = 1+4+1+(mp_table->MP_table_asset[i].Identifier_mapping->identifier_mapping_byte->asset_id->asset_id_length);
        }
        // 0x01 for URL
        // not handled now
        else if(mp_table->MP_table_asset[i].Identifier_mapping->identifier_type == 0x01)
        {
            identifier_mapping_length = 1;
        }
        table_length += identifier_mapping_length;
        //+ asset_type+reserved+asset_clock_relation_flag
        table_length += 4+1;

        //asset_location
        //+location_count+location_type
        table_length += 1+1*location_num;
        for(j=0;j<location_num;j++)
        {
            //0x00 for packet_id
            if(mp_table->MP_table_asset[i].asset_loaction->general_location_info[j].location_type == 0x00)
            {
                table_length += 2;
            }
            else
            {
            //other location_type
            //not handled now
                table_length += 0;
            }
        }

        //+asset_des
        table_length += 2+mp_table->MP_table_asset[i].asset_descriptors_length;

    }
    return table_length;
}

#endif// SMT_PROTOCAL_SIGNAL
