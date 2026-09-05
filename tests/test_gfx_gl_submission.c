#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
typedef unsigned int GLuint;
typedef unsigned int GLenum;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLboolean;
typedef float GLfloat;
#define GL_ARRAY_BUFFER 1
#define GL_STREAM_DRAW 2
#define GL_TRIANGLES 3
#define SHADER_FLAG_MAX 16
#define GFX_GL_QUEUE_BYTES 4096
#define GFX_GL_QUEUE_COMMANDS 32
static unsigned char uploaded[16384];
static size_t uploaded_size, active_stride;
static uint64_t trace;
static size_t uploads;
static void hash_bytes(const void *p,size_t n) {
    const unsigned char *b=p;
    while(n--) { trace^=*b++; trace*=1099511628211ULL; }
}
static void sys_fatal(const char *fmt,...) { (void)fmt; abort(); }
static void glBufferData(GLenum target,size_t bytes,const void *data,GLenum usage) {
    (void)target;(void)usage;
    assert(bytes<=sizeof(uploaded));
    memcpy(uploaded,data,bytes);uploaded_size=bytes;uploads++;
}
static void glDrawArrays(GLenum mode,GLint first,GLsizei count) {
    assert(first>=0 && count>=0 && active_stride>0);
    assert(((size_t)first+count)*active_stride<=uploaded_size);
    hash_bytes(&mode,sizeof(mode));hash_bytes(&count,sizeof(count));
    hash_bytes(uploaded+first*active_stride,count*active_stride);
}
static void glUseProgram(GLuint program) {
    const int tag=10; hash_bytes(&tag,sizeof(tag));
    hash_bytes(&program,sizeof(program));
}
static void glActiveTexture(GLenum texture) {
    const int tag=11; hash_bytes(&tag,sizeof(tag));
    hash_bytes(&texture,sizeof(texture));
}
static void glBindTexture(GLenum target, GLuint texture) {
    const int tag=12; hash_bytes(&tag,sizeof(tag));
    hash_bytes(&target,sizeof(target));
    hash_bytes(&texture,sizeof(texture));
}
static void glTexParameteri(GLenum target, GLenum pname, GLint param) {
    const int tag=13; hash_bytes(&tag,sizeof(tag));
    hash_bytes(&target,sizeof(target));
    hash_bytes(&pname,sizeof(pname));
    hash_bytes(&param,sizeof(param));
}
static void glEnable(GLenum cap) {
    const int tag=14; hash_bytes(&tag,sizeof(tag));
    hash_bytes(&cap,sizeof(cap));
}
static void glDisable(GLenum cap) {
    const int tag=15; hash_bytes(&tag,sizeof(tag));
    hash_bytes(&cap,sizeof(cap));
}
static void glDepthMask(GLboolean value) {
    const int tag=16; hash_bytes(&tag,sizeof(tag));
    hash_bytes(&value,sizeof(value));
}
static void glPolygonOffset(GLfloat factor, GLfloat units) {
    const int tag=17; hash_bytes(&tag,sizeof(tag));
    hash_bytes(&factor,sizeof(factor));
    hash_bytes(&units,sizeof(units));
}
static void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    const int tag=18; hash_bytes(&tag,sizeof(tag));
    hash_bytes(&x,sizeof(x));
    hash_bytes(&y,sizeof(y));
    hash_bytes(&width,sizeof(width));
    hash_bytes(&height,sizeof(height));
}
static void glScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    const int tag=19; hash_bytes(&tag,sizeof(tag));
    hash_bytes(&x,sizeof(x));
    hash_bytes(&y,sizeof(y));
    hash_bytes(&width,sizeof(width));
    hash_bytes(&height,sizeof(height));
}
static void glEnableVertexAttribArray(GLuint index) {
    const int tag=20; hash_bytes(&tag,sizeof(tag));
    hash_bytes(&index,sizeof(index));
}
static void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer) {
    const int tag=21; hash_bytes(&tag,sizeof(tag));
    hash_bytes(&index,sizeof(index));
    hash_bytes(&size,sizeof(size));
    hash_bytes(&type,sizeof(type));
    hash_bytes(&normalized,sizeof(normalized));
    hash_bytes(&stride,sizeof(stride));
    const uintptr_t p=(uintptr_t)pointer; hash_bytes(&p,sizeof(p));
    if(index==0) active_stride=stride;
}
static void glUniform1f(GLint location, GLfloat value) {
    const int tag=22; hash_bytes(&tag,sizeof(tag));
    hash_bytes(&location,sizeof(location));
    hash_bytes(&value,sizeof(value));
}
static void glUniform2f(GLint location, GLfloat x, GLfloat y) {
    const int tag=23; hash_bytes(&tag,sizeof(tag));
    hash_bytes(&location,sizeof(location));
    hash_bytes(&x,sizeof(x));
    hash_bytes(&y,sizeof(y));
}
static void glUniform3f(GLint location, GLfloat x, GLfloat y, GLfloat z) {
    const int tag=24; hash_bytes(&tag,sizeof(tag));
    hash_bytes(&location,sizeof(location));
    hash_bytes(&x,sizeof(x));
    hash_bytes(&y,sizeof(y));
    hash_bytes(&z,sizeof(z));
}
static void glUniform1i(GLint location, GLint value) {
    const int tag=25; hash_bytes(&tag,sizeof(tag));
    hash_bytes(&location,sizeof(location));
    hash_bytes(&value,sizeof(value));
}
static void glUniform1iv(GLint location, GLsizei count, const GLint *values) {
    const int tag=26; hash_bytes(&tag,sizeof(tag));
    hash_bytes(&location,sizeof(location));
    hash_bytes(&count,sizeof(count));
    hash_bytes(values,count*sizeof(*values));
}
static void glUniform1fv(GLint location, GLsizei count, const GLfloat *values) {
    const int tag=27; hash_bytes(&tag,sizeof(tag));
    hash_bytes(&location,sizeof(location));
    hash_bytes(&count,sizeof(count));
    hash_bytes(values,count*sizeof(*values));
}
#include "../src/pc/gfx/gfx_gl_submission.h"
static uint64_t exercise(bool queued) {
    trace=1469598103934665603ULL;
    uploads=0;
    if(queued) gfx_gl_queue_begin();
    for(int n=0;n<1000;n++) {
        const int stride=(4+n%8)*sizeof(float);
        float data[1024];
        for(size_t k=0;k<sizeof(data)/sizeof(*data);k++)data[k]=(float)(n*1024+k);
        GLint flags[SHADER_FLAG_MAX];GLfloat values[SHADER_FLAG_MAX];
        for(int k=0;k<SHADER_FLAG_MAX;k++){flags[k]=n+k;values[k]=(float)(n-k);}
        gfx_queue_UseProgram(n%7);
        gfx_queue_ActiveTexture(n%2);
        gfx_queue_BindTexture(7,n%19);
        gfx_queue_TexParameteri(7,8,n%3);
        gfx_queue_Enable(10);
        gfx_queue_Disable(11);
        gfx_queue_DepthMask(n%2);
        gfx_queue_PolygonOffset((float)n,-1);
        gfx_queue_Viewport(n,0,200,300);
        gfx_queue_Scissor(0,n,100,200);
        gfx_queue_EnableVertexAttribArray(0);
        gfx_queue_VertexAttribPointer(0,4,12,0,stride,NULL);
        gfx_queue_Uniform1f(0,(float)n);
        gfx_queue_Uniform2f(1,(float)n,2);
        gfx_queue_Uniform3f(2,(float)n,2,3);
        gfx_queue_Uniform1i(3,n);
        gfx_queue_Uniform1iv(4,SHADER_FLAG_MAX,flags);
        gfx_queue_Uniform1fv(5,SHADER_FLAG_MAX,values);
        gfx_gl_queue_draw(data,6*stride,stride,2);
        memset(data,0,sizeof(data));memset(flags,0,sizeof(flags));memset(values,0,sizeof(values));
        if(n%71==0)gfx_gl_queue_flush(); // resource mutation boundary
    }
    // No state changes: force vertex-capacity rollover separately.
    gfx_queue_VertexAttribPointer(0,4,12,0,16,NULL);
    float block[12]={0};
    for(int n=0;n<1000;n++){block[0]=(float)n;gfx_gl_queue_draw(block,sizeof(block),16,1);}
    // Oversized draws drain pending state and retain order.
    float large[1200]={1};
    gfx_gl_queue_draw(large,sizeof(large),16,100);
    gfx_gl_queue_end();
    return trace;
}
static uint64_t exercise_frame_boundaries(bool queued) {
    trace=1469598103934665603ULL;
    for (int eye=0; eye<20; ++eye) {
        if (queued) gfx_gl_queue_begin();
        gfx_queue_UseProgram(eye%3);
        gfx_queue_VertexAttribPointer(0,4,12,0,16,NULL);
        float vertices[12]={(float)eye};
        gfx_gl_queue_draw(vertices,sizeof(vertices),16,1);
        // Ending each eye must execute all draws before the host switches targets.
        gfx_gl_queue_end();
        assert(!gfx_gl_queue.active && !gfx_gl_queue.count && !gfx_gl_queue.used);
        hash_bytes(&eye,sizeof(eye)); // represents an external target change
        if (queued) gfx_gl_queue_begin();
        gfx_gl_queue_end(); // empty render pass
    }
    return trace;
}
int main(void) {
    uint64_t immediate=exercise(false);
    const size_t immediate_uploads=uploads;
    uint64_t queued=exercise(true);
    assert(immediate==queued);
    assert(uploads<immediate_uploads);
    printf("PASS: identical state/vertex trace; uploads %zu -> %zu; mixed layouts, copied uniforms, command/vertex rollover, oversized draw, resource barriers\n",immediate_uploads,uploads);
    gfx_gl_queue_shutdown();
    assert(!gfx_gl_queue.vertices && !gfx_gl_queue.commands);
    immediate=exercise_frame_boundaries(false);
    queued=exercise_frame_boundaries(true); // reinitialize after shutdown
    assert(immediate==queued);
    gfx_gl_queue_shutdown();
    puts("PASS: eye/frame boundaries, empty passes, shutdown and reinitialization");
    return 0;
}
