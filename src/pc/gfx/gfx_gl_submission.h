/* Private ordered GL submission queue. Include only after GL and platform headers.
 * All state values are copied when issued, not reconstructed from later globals.
 * Resource mutations and render-target boundaries MUST drain the queue first.
 */
#ifndef GFX_GL_SUBMISSION_H
#define GFX_GL_SUBMISSION_H
#ifndef GFX_GL_QUEUE_BYTES
#define GFX_GL_QUEUE_BYTES (4u * 1024u * 1024u)
#endif
#ifndef GFX_GL_QUEUE_COMMANDS
#define GFX_GL_QUEUE_COMMANDS 16384u
#endif
enum GfxGLCommandType {
    GFX_GL_UseProgram,
    GFX_GL_ActiveTexture,
    GFX_GL_BindTexture,
    GFX_GL_TexParameteri,
    GFX_GL_Enable,
    GFX_GL_Disable,
    GFX_GL_DepthMask,
    GFX_GL_PolygonOffset,
    GFX_GL_Viewport,
    GFX_GL_Scissor,
    GFX_GL_EnableVertexAttribArray,
    GFX_GL_VertexAttribPointer,
    GFX_GL_Uniform1f,
    GFX_GL_Uniform2f,
    GFX_GL_Uniform3f,
    GFX_GL_Uniform1i,
    GFX_GL_Uniform1iv,
    GFX_GL_Uniform1fv,
    GFX_GL_DRAW
};
struct GfxGLCommand {
    enum GfxGLCommandType type;
    GLint i[5];
    uintptr_t pointer;
    union { GLfloat f[SHADER_FLAG_MAX]; GLint values[SHADER_FLAG_MAX]; };
};
static struct {
    bool active;
    unsigned char *vertices;
    struct GfxGLCommand *commands;
    size_t used, count;
#ifdef __ANDROID__
    uint64_t window_start, upload_ns, replay_ns, draws, uploads;
#endif
} gfx_gl_queue;

static void gfx_gl_queue_flush(void) {
    if (!gfx_gl_queue.count) return;
#ifdef __ANDROID__
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const uint64_t start = (uint64_t)ts.tv_sec*1000000000ULL + ts.tv_nsec;
#endif
    if (gfx_gl_queue.used) {
        // One upload before any draw reads this storage. Renew it for every
        // chunk so earlier GPU work never observes an overwritten range.
        glBufferData(GL_ARRAY_BUFFER, gfx_gl_queue.used,
                     gfx_gl_queue.vertices, GL_STREAM_DRAW);
#ifdef __ANDROID__
        gfx_gl_queue.uploads++;
#endif
    }
#ifdef __ANDROID__
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const uint64_t uploaded = (uint64_t)ts.tv_sec*1000000000ULL + ts.tv_nsec;
#endif
    for (size_t n=0; n<gfx_gl_queue.count; ++n) {
        const struct GfxGLCommand *c = &gfx_gl_queue.commands[n];
        switch (c->type) {
            case GFX_GL_UseProgram: glUseProgram(c->i[0]); break;
            case GFX_GL_ActiveTexture: glActiveTexture(c->i[0]); break;
            case GFX_GL_BindTexture: glBindTexture(c->i[0], c->i[1]); break;
            case GFX_GL_TexParameteri: glTexParameteri(c->i[0], c->i[1], c->i[2]); break;
            case GFX_GL_Enable: glEnable(c->i[0]); break;
            case GFX_GL_Disable: glDisable(c->i[0]); break;
            case GFX_GL_DepthMask: glDepthMask(c->i[0]); break;
            case GFX_GL_PolygonOffset: glPolygonOffset(c->f[0], c->f[1]); break;
            case GFX_GL_Viewport: glViewport(c->i[0], c->i[1], c->i[2], c->i[3]); break;
            case GFX_GL_Scissor: glScissor(c->i[0], c->i[1], c->i[2], c->i[3]); break;
            case GFX_GL_EnableVertexAttribArray: glEnableVertexAttribArray(c->i[0]); break;
            case GFX_GL_VertexAttribPointer: glVertexAttribPointer(c->i[0], c->i[1], c->i[2], c->i[3], c->i[4], (const void *)c->pointer); break;
            case GFX_GL_Uniform1f: glUniform1f(c->i[0], c->f[0]); break;
            case GFX_GL_Uniform2f: glUniform2f(c->i[0], c->f[0], c->f[1]); break;
            case GFX_GL_Uniform3f: glUniform3f(c->i[0], c->f[0], c->f[1], c->f[2]); break;
            case GFX_GL_Uniform1i: glUniform1i(c->i[0], c->i[1]); break;
            case GFX_GL_Uniform1iv: glUniform1iv(c->i[0], c->i[1], c->values); break;
            case GFX_GL_Uniform1fv: glUniform1fv(c->i[0], c->i[1], c->f); break;
            case GFX_GL_DRAW:
                glDrawArrays(GL_TRIANGLES, c->i[0], c->i[1]);
#ifdef __ANDROID__
                gfx_gl_queue.draws++;
#endif
                break;
        }
    }
#ifdef __ANDROID__
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const uint64_t end = (uint64_t)ts.tv_sec*1000000000ULL + ts.tv_nsec;
    static unsigned diagnostic_chunks;
    if (diagnostic_chunks < 8) {
        const GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            __android_log_print(ANDROID_LOG_ERROR, "SM64CoopDXVR",
                "GL submission queue error: 0x%04x", error);
        }
        diagnostic_chunks++;
    }
    if (!gfx_gl_queue.window_start) gfx_gl_queue.window_start=start;
    gfx_gl_queue.upload_ns += uploaded-start;
    gfx_gl_queue.replay_ns += end-uploaded;
    if (end-gfx_gl_queue.window_start >= 1000000000ULL) {
        __android_log_print(ANDROID_LOG_INFO, "SM64CoopDXVR",
            "PERF_BULK window=%.3fs draws=%llu uploads=%llu vertexUpload=%.3fms replay=%.3fms",
            (double)(end-gfx_gl_queue.window_start)/1e9,
            (unsigned long long)gfx_gl_queue.draws,
            (unsigned long long)gfx_gl_queue.uploads,
            (double)gfx_gl_queue.upload_ns/1e6,
            (double)gfx_gl_queue.replay_ns/1e6);
        gfx_gl_queue.window_start=end;
        gfx_gl_queue.upload_ns=gfx_gl_queue.replay_ns=0;
        gfx_gl_queue.draws=gfx_gl_queue.uploads=0;
    }
#endif
    gfx_gl_queue.count=gfx_gl_queue.used=0;
}
static struct GfxGLCommand *gfx_gl_queue_command(enum GfxGLCommandType type) {
    if (gfx_gl_queue.count == GFX_GL_QUEUE_COMMANDS) gfx_gl_queue_flush();
    struct GfxGLCommand *c=&gfx_gl_queue.commands[gfx_gl_queue.count++];
    c->type=type;
    return c;
}
static void gfx_gl_queue_begin(void) {
    gfx_gl_queue_flush();
    if (!gfx_gl_queue.vertices) {
        gfx_gl_queue.vertices=malloc(GFX_GL_QUEUE_BYTES);
        gfx_gl_queue.commands=malloc(GFX_GL_QUEUE_COMMANDS*sizeof(*gfx_gl_queue.commands));
        if (!gfx_gl_queue.vertices || !gfx_gl_queue.commands)
            sys_fatal("out of memory allocating GL submission queue");
    }
    gfx_gl_queue.active=true;
}
static void gfx_gl_queue_end(void) {
    gfx_gl_queue_flush();
    gfx_gl_queue.active=false;
}
static void gfx_gl_queue_shutdown(void) {
    gfx_gl_queue_end();
    free(gfx_gl_queue.vertices);
    free(gfx_gl_queue.commands);
    memset(&gfx_gl_queue, 0, sizeof(gfx_gl_queue));
}

static void gfx_queue_UseProgram(GLuint program) {
    if (!gfx_gl_queue.active) { glUseProgram(program); return; }
    struct GfxGLCommand *c=gfx_gl_queue_command(GFX_GL_UseProgram);
    c->i[0]=program;
}

static void gfx_queue_ActiveTexture(GLenum texture) {
    if (!gfx_gl_queue.active) { glActiveTexture(texture); return; }
    struct GfxGLCommand *c=gfx_gl_queue_command(GFX_GL_ActiveTexture);
    c->i[0]=texture;
}

static void gfx_queue_BindTexture(GLenum target, GLuint texture) {
    if (!gfx_gl_queue.active) { glBindTexture(target, texture); return; }
    struct GfxGLCommand *c=gfx_gl_queue_command(GFX_GL_BindTexture);
    c->i[0]=target; c->i[1]=texture;
}

static void gfx_queue_TexParameteri(GLenum target, GLenum pname, GLint param) {
    if (!gfx_gl_queue.active) { glTexParameteri(target, pname, param); return; }
    struct GfxGLCommand *c=gfx_gl_queue_command(GFX_GL_TexParameteri);
    c->i[0]=target; c->i[1]=pname; c->i[2]=param;
}

static void gfx_queue_Enable(GLenum cap) {
    if (!gfx_gl_queue.active) { glEnable(cap); return; }
    struct GfxGLCommand *c=gfx_gl_queue_command(GFX_GL_Enable);
    c->i[0]=cap;
}

static void gfx_queue_Disable(GLenum cap) {
    if (!gfx_gl_queue.active) { glDisable(cap); return; }
    struct GfxGLCommand *c=gfx_gl_queue_command(GFX_GL_Disable);
    c->i[0]=cap;
}

static void gfx_queue_DepthMask(GLboolean value) {
    if (!gfx_gl_queue.active) { glDepthMask(value); return; }
    struct GfxGLCommand *c=gfx_gl_queue_command(GFX_GL_DepthMask);
    c->i[0]=value;
}

static void gfx_queue_PolygonOffset(GLfloat factor, GLfloat units) {
    if (!gfx_gl_queue.active) { glPolygonOffset(factor, units); return; }
    struct GfxGLCommand *c=gfx_gl_queue_command(GFX_GL_PolygonOffset);
    c->f[0]=factor; c->f[1]=units;
}

static void gfx_queue_Viewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    if (!gfx_gl_queue.active) { glViewport(x, y, width, height); return; }
    struct GfxGLCommand *c=gfx_gl_queue_command(GFX_GL_Viewport);
    c->i[0]=x; c->i[1]=y; c->i[2]=width; c->i[3]=height;
}

static void gfx_queue_Scissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    if (!gfx_gl_queue.active) { glScissor(x, y, width, height); return; }
    struct GfxGLCommand *c=gfx_gl_queue_command(GFX_GL_Scissor);
    c->i[0]=x; c->i[1]=y; c->i[2]=width; c->i[3]=height;
}

static void gfx_queue_EnableVertexAttribArray(GLuint index) {
    if (!gfx_gl_queue.active) { glEnableVertexAttribArray(index); return; }
    struct GfxGLCommand *c=gfx_gl_queue_command(GFX_GL_EnableVertexAttribArray);
    c->i[0]=index;
}

static void gfx_queue_VertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer) {
    if (!gfx_gl_queue.active) { glVertexAttribPointer(index, size, type, normalized, stride, pointer); return; }
    struct GfxGLCommand *c=gfx_gl_queue_command(GFX_GL_VertexAttribPointer);
    c->i[0]=index; c->i[1]=size; c->i[2]=type; c->i[3]=normalized; c->i[4]=stride; c->pointer=(uintptr_t)pointer;
}

static void gfx_queue_Uniform1f(GLint location, GLfloat value) {
    if (!gfx_gl_queue.active) { glUniform1f(location, value); return; }
    struct GfxGLCommand *c=gfx_gl_queue_command(GFX_GL_Uniform1f);
    c->i[0]=location; c->f[0]=value;
}

static void gfx_queue_Uniform2f(GLint location, GLfloat x, GLfloat y) {
    if (!gfx_gl_queue.active) { glUniform2f(location, x, y); return; }
    struct GfxGLCommand *c=gfx_gl_queue_command(GFX_GL_Uniform2f);
    c->i[0]=location; c->f[0]=x; c->f[1]=y;
}

static void gfx_queue_Uniform3f(GLint location, GLfloat x, GLfloat y, GLfloat z) {
    if (!gfx_gl_queue.active) { glUniform3f(location, x, y, z); return; }
    struct GfxGLCommand *c=gfx_gl_queue_command(GFX_GL_Uniform3f);
    c->i[0]=location; c->f[0]=x; c->f[1]=y; c->f[2]=z;
}

static void gfx_queue_Uniform1i(GLint location, GLint value) {
    if (!gfx_gl_queue.active) { glUniform1i(location, value); return; }
    struct GfxGLCommand *c=gfx_gl_queue_command(GFX_GL_Uniform1i);
    c->i[0]=location; c->i[1]=value;
}

static void gfx_queue_Uniform1iv(GLint location, GLsizei count, const GLint *values) {
    if (count < 0 || count > SHADER_FLAG_MAX) {
        gfx_gl_queue_flush();
        glUniform1iv(location, count, values);
        return;
    }
    if (!gfx_gl_queue.active) { glUniform1iv(location, count, values); return; }
    struct GfxGLCommand *c=gfx_gl_queue_command(GFX_GL_Uniform1iv);
    c->i[0]=location; c->i[1]=count; memcpy(c->values, values, count*sizeof(*values));
}

static void gfx_queue_Uniform1fv(GLint location, GLsizei count, const GLfloat *values) {
    if (count < 0 || count > SHADER_FLAG_MAX) {
        gfx_gl_queue_flush();
        glUniform1fv(location, count, values);
        return;
    }
    if (!gfx_gl_queue.active) { glUniform1fv(location, count, values); return; }
    struct GfxGLCommand *c=gfx_gl_queue_command(GFX_GL_Uniform1fv);
    c->i[0]=location; c->i[1]=count; memcpy(c->f, values, count*sizeof(*values));
}

static void gfx_gl_queue_draw(const float *vertices, size_t bytes,
                              size_t stride, size_t triangles) {
    if (!gfx_gl_queue.active || bytes > GFX_GL_QUEUE_BYTES) {
        gfx_gl_queue_flush();
        glBufferData(GL_ARRAY_BUFFER, bytes, vertices, GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, triangles*3);
        return;
    }
    // Drain commands BEFORE computing the offset: command exhaustion may also
    // drain vertices. Layouts differ, so align to the current vertex stride.
    if (gfx_gl_queue.count == GFX_GL_QUEUE_COMMANDS) gfx_gl_queue_flush();
    size_t padding=(stride-gfx_gl_queue.used%stride)%stride;
    if (bytes > GFX_GL_QUEUE_BYTES-gfx_gl_queue.used ||
        padding > GFX_GL_QUEUE_BYTES-gfx_gl_queue.used-bytes) {
        gfx_gl_queue_flush();
        padding=0;
    }
    const size_t offset=gfx_gl_queue.used+padding;
    // Upload padding is initialized, never stale host memory.
    memset(gfx_gl_queue.vertices+gfx_gl_queue.used, 0, padding);
    memcpy(gfx_gl_queue.vertices+offset, vertices, bytes);
    gfx_gl_queue.used=offset+bytes;
    struct GfxGLCommand *c=gfx_gl_queue_command(GFX_GL_DRAW);
    c->i[0]=(GLint)(offset/stride);
    c->i[1]=(GLint)(triangles*3);
}
#endif
