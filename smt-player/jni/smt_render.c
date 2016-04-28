#include "smt_render.h"
#include "libavutil/pixfmt.h"

#define OFFSET(x) offsetof(OpenclContext, x)

const AVClass opengl_class = {
    .class_name                = "opengl render",
    .option                    = NULL,
    .item_name                 = av_default_item_name,
    .version                   = LIBAVUTIL_VERSION_INT,
    .log_level_offset_offset   = offsetof(SMTRenderContext, log_offset),
    .parent_log_context_offset = offsetof(SMTRenderContext, log_ctx),

};

static void smt_check_error(SMTRenderContext *h, const char* msg)
{
    GLint err;
    do{
        err = glGetError();
        if(err)
            av_log(h, AV_LOG_FATAL, "GL internal error in function %s. error code: 0x%x\n", msg, err);
    }while(err);
}

static GLuint smt_create_shader(SMTRenderContext *h, GLenum shaderType, const char* pSource)
{
    GLuint shader = glCreateShader(shaderType);  
    if (shader) {  
        glShaderSource(shader, 1, &pSource, NULL);  
        glCompileShader(shader);  
        GLint compiled = 0;  
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);  
        if (!compiled) {  
            GLint infoLen = 0;  
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);  
            if (infoLen) {  
                char* buf = av_mallocz(infoLen);  
                if (buf) {  
                    glGetShaderInfoLog(shader, infoLen, NULL, buf);   
                    av_log(h, AV_LOG_ERROR, "Could not compile shader %d: %s", shaderType, buf);
                    av_freep(&buf);  
                }  
                glDeleteShader(shader);  
                shader = 0;  
            }  
        }  
    }  
    return shader;
}


static GLuint smt_create_program(SMTRenderContext *h)
{
    char *vertex_shader_code = "attribute vec4 aPosition;\n"  
        "attribute vec2 aTextureCoord;\n"  
        "varying vec2 vTextureCoord;\n"  
        "void main() {\n"  
        "  gl_Position = aPosition;\n"  
        "  vTextureCoord = aTextureCoord;\n"  
        "}\n";  

    char *fragment_sharder_code = "precision highp float;\n"  
        "uniform sampler2D Ytex;\n"  
        "uniform sampler2D Utex,Vtex;\n"  
        "varying vec2 vTextureCoord;\n"  
        "void main(void) {\n"  
        "  float nx,ny,r,g,b,y,u,v;\n"  
        "  highp vec4 txl,ux,vx;"  
        "  nx=vTextureCoord[0];\n"  
        "  ny=vTextureCoord[1];\n"  
        "  y=texture2D(Ytex,vec2(nx,ny)).r;\n"  
        "  u=texture2D(Utex,vec2(nx,ny)).r;\n"  
        "  v=texture2D(Vtex,vec2(nx,ny)).r;\n"  
          
        //"  y = v;\n"+  
        "  y=1.1643*(y-0.0625);\n"  
        "  u=u-0.5;\n"  
        "  v=v-0.5;\n"  
          
        "  r=y+1.5958*v;\n"  
        "  g=y-0.39173*u-0.81290*v;\n"  
        "  b=y+2.017*u;\n"  
        "  gl_FragColor=vec4(r,g,b,1.0);\n"  
        "}\n";

        

    GLuint vertex_shader = smt_create_shader(h, GL_VERTEX_SHADER, vertex_shader_code);
    GLuint fragment_shader = smt_create_shader(h, GL_VERTEX_SHADER, fragment_sharder_code);
    GLuint program = glCreateProgram();
    smt_check_error(h, "glCreateProgram()");
    
    if(vertex_shader && fragment_shader && program){
        glAttachShader(program, vertex_shader);
        smt_check_error(h, "glAttachShader(program, vertex_shader)");
        glAttachShader(program, fragment_shader);
        smt_check_error(h, "glAttachShader(program, fragment_shader)");
        glLinkProgram(program);  
        GLint linkStatus = GL_FALSE;  
        glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);  
        if (linkStatus != GL_TRUE) {  
            GLint bufLength = 0;  
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);  
            if (bufLength) {  
                char* buf = av_mallocz(bufLength);  
                if (buf) {  
                    glGetProgramInfoLog(program, bufLength, NULL, buf);  
                    av_log(h, AV_LOG_ERROR, "Could not link program: %s", buf);
                    av_freep(&buf);  
                }  
            }  
            glDeleteProgram(program);  
            program = 0;  
        }  
    }
    return program;
}

static void smt_create_YUV_texture(SMTRenderContext *h)
{
    glDeleteTextures(h->texture_size, h->textures);
    smt_check_error(h, "glDeleteTextures(h->texture_size, h->textures)");
    glGenTextures(h->texture_size, h->textures);
    smt_check_error(h, "glGenTextures(h->texture_size, h->textures)");
    //Y
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, h->textures[0]);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, h->width, h->height, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);

    //U
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, h->textures[1]);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, h->width/2, h->height/2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);

    //V
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, h->textures[2]);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, h->width/2, h->height/2, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL); 

}


static void smt_update_YUV_texture(SMTRenderContext *h, void *data)
{
    glActiveTexture(GL_TEXTURE0);  
    glBindTexture(GL_TEXTURE_2D, h->textures[0]);  
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, h->width, h->height, GL_LUMINANCE, GL_UNSIGNED_BYTE,  
                    data);  
      
    glActiveTexture(GL_TEXTURE1);  
    glBindTexture(GL_TEXTURE_2D, h->textures[1]);  
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, h->width / 2, h->height / 2, GL_LUMINANCE,  
                    GL_UNSIGNED_BYTE, (char *)data + h->width * h->height);  
      
    glActiveTexture(GL_TEXTURE2);  
    glBindTexture(GL_TEXTURE_2D, h->textures[2]);  
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, h->width / 2, h->height / 2, GL_LUMINANCE,  
                    GL_UNSIGNED_BYTE, (char *)data + h->width * h->height * 5 / 4);  

}


//interface

GLboolean smt_gl_setup(SMTRenderContext *h, enum AVPixelFormat fmt)
{
    const GLfloat vertices[20] = {  
        // X, Y, Z, U, V  
        -1, -1, 0, 1, 0, // Bottom Left  
        1, -1, 0, 0, 0, //Bottom Right  
        1, 1, 0, 0, 1, //Top Right  
        -1, 1, 0, 1, 1 }; //Top Left

    GLuint program = smt_create_program(h);
    if(!program){
        av_log(h, AV_LOG_ERROR, "create gl program failed.");
        return GL_FALSE;
    }

    int positionHandle = glGetAttribLocation(program, "aPosition");  
    smt_check_error(h, "glGetAttribLocation aPosition");  
    if (positionHandle == -1)
        return GL_FALSE;  
      
    int textureHandle = glGetAttribLocation(program, "aTextureCoord");  
    smt_check_error(h, "glGetAttribLocation aTextureCoord");  
    if (textureHandle == -1)   
        return GL_FALSE;  
      
    // set the vertices array in the shader  
    // _vertices contains 4 vertices with 5 coordinates.  
    // 3 for (xyz) for the vertices and 2 for the texture  
    glVertexAttribPointer(positionHandle, 3, GL_FLOAT, GL_FALSE,  
                          5 * sizeof(GLfloat), vertices);  
    smt_check_error(h, "glVertexAttribPointer aPosition");  
      
    glEnableVertexAttribArray(positionHandle);  
    smt_check_error(h, "glEnableVertexAttribArray positionHandle");  
      
    // set the texture coordinate array in the shader  
    // _vertices contains 4 vertices with 5 coordinates.  
    // 3 for (xyz) for the vertices and 2 for the texture  
    glVertexAttribPointer(textureHandle, 2, GL_FLOAT, GL_FALSE, 5  
                          * sizeof(GLfloat), &vertices[3]);  
    smt_check_error(h, "glVertexAttribPointer maTextureHandle");  
    glEnableVertexAttribArray(textureHandle);  
    smt_check_error(h, "glEnableVertexAttribArray textureHandle");  
      
    glUseProgram(program);  
    int i = glGetUniformLocation(program, "Ytex");  
    smt_check_error(h, "glGetUniformLocation");  
    glUniform1i(i, 0); /* Bind Ytex to texture unit 0 */  
    smt_check_error(h, "glUniform1i Ytex");  
      
    i = glGetUniformLocation(program, "Utex");  
    smt_check_error(h, "glGetUniformLocation Utex");  
    glUniform1i(i, 1); /* Bind Utex to texture unit 1 */  
    smt_check_error(h, "glUniform1i Utex");  
      
    i = glGetUniformLocation(program, "Vtex");  
    smt_check_error(h, "glGetUniformLocation");  
    glUniform1i(i, 2); /* Bind Vtex to texture unit 2 */  
    smt_check_error(h, "glUniform1i");  
      
    glViewport(0, 0, h->width, h->height);  
    smt_check_error(h, "glViewport"); 

    switch(fmt){
        case AV_PIX_FMT_YUV420P:
            h->texture_size = 3;
            h->textures = av_mallocz(sizeof(GLuint) * 3);
            smt_create_YUV_texture(h);
            break;
        default:
            av_log(h, AV_LOG_ERROR, "pixel format %d is not supported", fmt);
            return GL_FALSE;
    }
    
    return GL_TRUE;
}


void smt_gl_draw(SMTRenderContext *h, enum AVPixelFormat fmt, GLsizei width, GLsizei height, void *data)
{
    if (h->width != width || h->height != height) {
        h->width = width;
        h->height = height;
        switch(fmt){
            case AV_PIX_FMT_YUV420P:
                smt_create_YUV_texture(h);
                break;
            default:
                av_log(h, AV_LOG_ERROR, "failure drawing. pixel format %d is not supported", fmt);
                return;
        }

    }  
    smt_update_YUV_texture(h, data); 
      
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    smt_check_error(h, "glDrawArrays");  

}
