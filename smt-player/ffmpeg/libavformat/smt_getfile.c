#include "smt_getfile.h"
#include "smt_signal.h"

#if 0
int cmp(char *p1,char *p2) /* 字符串比较函数 */
{
    int i=0;
    while (*(p1+i)==*(p2+i))
        if (*(p1+i++)=='\0') return 0; /* 这里是先判断*(p1+i)=='\0'，然后再是i++ */
    return (*(p1+i)-*(p2+i));
}

void sort(char **fileNameList, int count) /*排序函数*/
{
    char *temp;
    int i,j;
    for(i=0;i<count-1;i++)/* 选择排序 */
        for(j=i+1;j<count;j++)
            if(cmp(fileNameList[i],fileNameList[j])>0)
            {
                temp=fileNameList[i];/* 交换的是字符串的地址，不是字符串的内容，可更改 */
                fileNameList[i]=fileNameList[j];
                fileNameList[j]=temp;
            }
}

//获取指定目录下的所有文件列表 author:wangchangshaui jlu
//this function can get result in fileNameList
int getFileNameArray(const char *path, u_int32_t* fileCount,char ***fileNameList)
{
    int count = 0;
    //char **fileNameList = NULL;
    struct dirent* ent = NULL;
    DIR *pDir;
    char dir[512];
    struct stat statbuf;

    //打开目录
    if ((pDir = opendir(path)) == NULL)
    {
        PRINTF("Cannot open directory:%s\n", path);
        return -1;
    }
    //读取目录
    while ((ent = readdir(pDir)) != NULL)
    { //统计当前文件夹下有多少文件（不包括文件夹）
        //得到读取文件的绝对路径名
        snprintf(dir, 512, "%s/%s", path, ent->d_name);
        //得到文件信息
        lstat(dir, &statbuf);
        //判断是目录还是文件
        if (!S_ISDIR(statbuf.st_mode))
        {
            count++;
        }
    } //while
    //关闭目录
    closedir(pDir);
    //  PRINTF("共%d个文件\n", count);

    //开辟字符指针数组，用于下一步的开辟容纳文件名字符串的空间
    if (((*fileNameList) = (char**) malloc(sizeof(char*) * count)) == NULL)
    {
        PRINTF("Malloc heap failed!\n");
        return -1;
    }

    //打开目录
    if ((pDir = opendir(path)) == NULL)
    {
        PRINTF("Cannot open directory:%s\n", path);
        return -1;
    }
    //读取目录
    int i;
    for (i = 0; (ent = readdir(pDir)) != NULL && i < count;)
    {
        if (strlen(ent->d_name) <= 0)
        {
            continue;
        }
        //得到读取文件的绝对路径名
        snprintf(dir, 512, "%s/%s", path, ent->d_name);
        //得到文件信息
        lstat(dir, &statbuf);
        //判断是目录还是文件
        if (!S_ISDIR(statbuf.st_mode))
        {
            if (((*fileNameList)[i] = (char*) malloc(strlen(ent->d_name) + 1))
                    == NULL)
            {
                PRINTF("Malloc heap failed!\n");
                return -1;
            }
            //            char dir_buf[512];
            //            memset(dir_buf, 0, strlen(dir_buf));
            //            memcpy(dir_buf,path,strlen(path));
            //            memcpy(&dir_buf[strlen(path)],ent->d_name,strlen(ent->d_name) + 1);
            //
            //            memset(fileNameList[i], 0, strlen(path)+strlen(ent->d_name) + 1);
            //            memcpy(fileNameList[i],dir_buf,strlen(dir_buf) + 1);
            //            //strcpy(fileNameList[i], dir_buf);

            memset((*fileNameList)[i], 0, strlen(ent->d_name) + 1);
            strcpy((*fileNameList)[i], ent->d_name);
            // PRINTF("第%d个文件:%s\n", i, (*fileNameList)[i]);


            i++;
        }

    } //for
    //PRINTF("%s目录总计%d个文件\n", path, i);
    //关闭目录
    closedir(pDir);
    *fileCount = count;
    //q_sortB(fileNameList, count);
    sort((*fileNameList), count);
    // for(i=0;i<count;i++)
    //    {

    //        PRINTF("bbbb第%d个文件:%s\n",i,(*fileNameList)[i]);
    //    }

    return 1;
}

//获取指定目录下的所有文件列表 author:wangchangshaui jlu
//this function like getFileNameArray, you  can get result by return
char ** getFileNameArray2(const char *path, u_int32_t* fileCount)
{
    int count = 0;
    char **fileNameList = NULL;
    //char **fileNameList = NULL;
    struct dirent* ent = NULL;
    DIR *pDir;
    char dir[512];
    struct stat statbuf;

    //打开目录
    if ((pDir = opendir(path)) == NULL)
    {
        PRINTF("Cannot open directory:%s\n", path);
        return NULL;
    }
    //读取目录
    while ((ent = readdir(pDir)) != NULL)
    { //统计当前文件夹下有多少文件（不包括文件夹）
        //得到读取文件的绝对路径名
        snprintf(dir, 512, "%s/%s", path, ent->d_name);
        //得到文件信息
        lstat(dir, &statbuf);
        //判断是目录还是文件
        if (!S_ISDIR(statbuf.st_mode))
        {
            count++;
        }
    } //while
    //关闭目录
    closedir(pDir);
    //  PRINTF("共%d个文件\n", count);

    //开辟字符指针数组，用于下一步的开辟容纳文件名字符串的空间
    if ((fileNameList = (char**) malloc(sizeof(char*) * count)) == NULL)
    {
        PRINTF("Malloc heap failed!\n");
        return NULL;
    }

    //打开目录
    if ((pDir = opendir(path)) == NULL)
    {
        PRINTF("Cannot open directory:%s\n", path);
        return NULL;
    }
    //读取目录
    int i;
    for (i = 0; (ent = readdir(pDir)) != NULL && i < count;)
    {
        if (strlen(ent->d_name) <= 0)
        {
            continue;
        }
        //得到读取文件的绝对路径名
        snprintf(dir, 512, "%s/%s", path, ent->d_name);
        //得到文件信息
        lstat(dir, &statbuf);
        //判断是目录还是文件
        if (!S_ISDIR(statbuf.st_mode))
        {
            if ((fileNameList[i] = (char*) malloc(strlen(ent->d_name) + 1))
                    == NULL)
            {
                PRINTF("Malloc heap failed!\n");
                return NULL;
            }
            //            char dir_buf[512];
            //            memset(dir_buf, 0, strlen(dir_buf));
            //            memcpy(dir_buf,path,strlen(path));
            //            memcpy(&dir_buf[strlen(path)],ent->d_name,strlen(ent->d_name) + 1);
            //
            //            memset(fileNameList[i], 0, strlen(path)+strlen(ent->d_name) + 1);
            //            memcpy(fileNameList[i],dir_buf,strlen(dir_buf) + 1);
            //            //strcpy(fileNameList[i], dir_buf);

            memset(fileNameList[i], 0, strlen(ent->d_name) + 1);
            strcpy(fileNameList[i], ent->d_name);
            PRINTF("第%d个文件:%s\n", i, fileNameList[i]);


            i++;
        }
    } //for
    //关闭目录
    closedir(pDir);
    *fileCount = count;
    //q_sortB(fileNameList, count);
    sort(fileNameList, count);
    for(i=0;i<count;i++)
    {

        PRINTF("bbbb第%d个文件:%s\n",i,fileNameList[i]);
    }

    return fileNameList;
}
#endif

void get_full_path(const char *path , char * filename, char *fullpath )
{
    if(fullpath == NULL  || path == NULL || filename == NULL) {
        PRINTF("null point error\n");
        return;
    }
    char *dir_buf = fullpath; 
    memset(dir_buf, 0, MAX_PATH_LEN);
    if(strlen(path) + strlen(filename) + 1 > MAX_PATH_LEN) {
        PRINTF("out of range error\n");
        return;
    }
    memcpy(dir_buf, path, strlen(path));
    memcpy(&dir_buf[strlen(path)], filename, strlen(filename) + 1);
    //PRINTF("begin send %s\n", dir_buf);
}

char* get_full_path2(const char *path ,char * filename)
{

    static char dir_buf[512];
    memset(dir_buf, 0, strlen(dir_buf));
    memcpy(dir_buf,path,strlen(path));
    memcpy(&dir_buf[strlen(path)],filename,strlen(filename) + 1);
    //PRINTF("fullpath%s\n",dir_buf);
    return (char *)dir_buf;

}

int ReadFile(char * path, char ** fdata, u_int16_t *length)
{
    FILE * pfile;
    char * data;

    pfile = fopen(path, "rb");
    if (pfile == NULL)
    {
        return -1;
    }
    fseek(pfile, 0, SEEK_END);
    *length = ftell(pfile);
    data = (char *)malloc((*length + 1) * sizeof(char));
    rewind(pfile);
    *length = fread(data, 1, *length, pfile);
    data[*length] = '\0';
    fclose(pfile);
    *fdata=data;
    return 1;
}

char* strcatex(char* str1, char* str2)
{
    char* buf = (char*) malloc(strlen(str1) + strlen(str2) + 1);
    sprintf(buf, "%s", str1);
    strcat(buf, str2);
    return buf;
}
