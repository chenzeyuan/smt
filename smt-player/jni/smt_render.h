#ifndef SMT_RENDER_H
#define SMT_RENDER_H

#if defined(__ANDROID__)
#include <jni.h>
#include <android/log.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include "libavutil/log.h"
#else
#include <GL/glut.h>
#endif

extern const AVClass opengl_class;

typedef struct SMTRenderContext{
    const AVClass *av_class;
    int log_offset;
    void *log_ctx;
    GLsizei   texture_size;
    GLuint    *textures;
    GLsizei   width, height;
} SMTRenderContext;



GLboolean smt_gl_setup(SMTRenderContext *h, enum AVPixelFormat fmt);
void smt_gl_draw(SMTRenderContext *h, enum AVPixelFormat fmt, GLsizei width, GLsizei height, void *data);



#endif
