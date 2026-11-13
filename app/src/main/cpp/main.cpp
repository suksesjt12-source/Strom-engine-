
#include <android/log.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <time.h>
#include <cmath>
#include <algorithm>

#define LOG(...) __android_log_print(ANDROID_LOG_INFO,"STORM",__VA_ARGS__)

struct Vec3 {
    float x,y,z;
};

struct Mat4 {
    float m[16]{};
};

Mat4 identity() {
    Mat4 r;
    r.m[0]=r.m[5]=r.m[10]=r.m[15]=1;
    return r;
}

Mat4 mul(Mat4 a,Mat4 b) {
    Mat4 r;
    for(int c=0;c<4;c++)
        for(int row=0;row<4;row++)
            for(int k=0;k<4;k++)
                r.m[c*4+row]+=a.m[k*4+row]*b.m[c*4+k];
    return r;
}

Mat4 translate(float x,float y,float z) {
    Mat4 r=identity();
    r.m[12]=x;
    r.m[13]=y;
    r.m[14]=z;
    return r;
}

Mat4 scale(float x,float y,float z) {
    Mat4 r=identity();
    r.m[0]=x;
    r.m[5]=y;
    r.m[10]=z;
    return r;
}

Mat4 perspective(float f,float aspect,float n,float far) {
    Mat4 r;
    float t=1.0f/tanf(f*0.5f);
    r.m[0]=t/aspect;
    r.m[5]=t;
    r.m[10]=(far+n)/(n-far);
    r.m[11]=-1;
    r.m[14]=(2*far*n)/(n-far);
    return r;
}

Mat4 camera() {
    Mat4 r=identity();
    r.m[5]=0.8944f;
    r.m[6]=-0.4472f;
    r.m[9]=0.4472f;
    r.m[10]=0.8944f;
    r.m[13]=-2.6832f;
    r.m[14]=-8.944f;
    return r;
}

const char* VS=R"(
#version 300 es
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;

uniform mat4 uMVP;
uniform mat4 uModel;

out vec3 normal;

void main() {
    gl_Position=uMVP*vec4(aPos,1.0);
    normal=mat3(uModel)*aNormal;
}
)";

const char* FS=R"(
#version 300 es
precision highp float;

in vec3 normal;
uniform vec3 uColor;

out vec4 FragColor;

void main() {
    vec3 N=normalize(normal);
    vec3 L=normalize(vec3(-0.5,0.8,0.6));
    float light=max(dot(N,L),0.0);

    float shade=light>0.75 ? 1.0 :
                light>0.35 ? 0.75 : 0.42;

    FragColor=vec4(uColor*shade,1.0);
}
)";

float cube[]={
-0.5,-0.5,0.5,0,0,1,
0.5,-0.5,0.5,0,0,1,
0.5,0.5,0.5,0,0,1,
-0.5,-0.5,0.5,0,0,1,
0.5,0.5,0.5,0,0,1,
-0.5,0.5,0.5,0,0,1,

-0.5,-0.5,-0.5,0,0,-1,
-0.5,0.5,-0.5,0,0,-1,
0.5,0.5,-0.5,0,0,-1,
-0.5,-0.5,-0.5,0,0,-1,
0.5,0.5,-0.5,0,0,-1,
0.5,-0.5,-0.5,0,0,-1,

-0.5,-0.5,-0.5,-1,0,0,
-0.5,-0.5,0.5,-1,0,0,
-0.5,0.5,0.5,-1,0,0,
-0.5,-0.5,-0.5,-1,0,0,
-0.5,0.5,0.5,-1,0,0,
-0.5,0.5,-0.5,-1,0,0,

0.5,-0.5,-0.5,1,0,0,
0.5,0.5,0.5,1,0,0,
0.5,-0.5,0.5,1,0,0,
0.5,-0.5,-0.5,1,0,0,
0.5,0.5,-0.5,1,0,0,
0.5,0.5,0.5,1,0,0,

-0.5,0.5,-0.5,0,1,0,
-0.5,0.5,0.5,0,1,0,
0.5,0.5,0.5,0,1,0,
-0.5,0.5,-0.5,0,1,0,
0.5,0.5,0.5,0,1,0,
0.5,0.5,-0.5,0,1,0,

-0.5,-0.5,-0.5,0,-1,0,
0.5,-0.5,0.5,0,-1,0,
-0.5,-0.5,0.5,0,-1,0,
-0.5,-0.5,-0.5,0,-1,0,
0.5,-0.5,-0.5,0,-1,0,
0.5,-0.5,0.5,0,-1,0
};

GLuint program=0,vao=0,vbo=0;

EGLDisplay display=EGL_NO_DISPLAY;
EGLSurface surface=EGL_NO_SURFACE;
EGLContext context=EGL_NO_CONTEXT;

int W=1280,H=720;

float playerX=-1.5f;
float enemyX=1.5f;

float playerHP=100;
float enemyHP=100;

float attackTimer=0;
float enemyTimer=0;
float attackAnim=0;

float moveInput=0;

bool attackHeld=false;

int movePointer=-1;
int attackPointer=-1;

double lastTime=0;

GLuint shader(GLenum type,const char* src) {
    GLuint s=glCreateShader(type);
    glShaderSource(s,1,&src,nullptr);
    glCompileShader(s);

    GLint ok=0;
    glGetShaderiv(s,GL_COMPILE_STATUS,&ok);

    if(!ok) {
        char log[1024];
        glGetShaderInfoLog(s,1024,nullptr,log);
        LOG("Shader error: %s",log);
    }

    return s;
}

void initGraphics() {
    GLuint vs=shader(GL_VERTEX_SHADER,VS);
    GLuint fs=shader(GL_FRAGMENT_SHADER,FS);

    program=glCreateProgram();
    glAttachShader(program,vs);
    glAttachShader(program,fs);
    glLinkProgram(program);

    glDeleteShader(vs);
    glDeleteShader(fs);

    glGenVertexArrays(1,&vao);
    glBindVertexArray(vao);

    glGenBuffers(1,&vbo);
    glBindBuffer(GL_ARRAY_BUFFER,vbo);

    glBufferData(
        GL_ARRAY_BUFFER,
        sizeof(cube),
        cube,
        GL_STATIC_DRAW
    );

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0,3,GL_FLOAT,GL_FALSE,
        6*sizeof(float),(void*)0
    );

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1,3,GL_FLOAT,GL_FALSE,
        6*sizeof(float),(void*)(3*sizeof(float))
    );

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    glClearColor(0.35f,0.55f,0.82f,1);
}

void draw(
    Mat4 vp,
    float x,float y,float z,
    float sx,float sy,float sz,
    Vec3 color
) {
    Mat4 model=mul(
        translate(x,y,z),
        scale(sx,sy,sz)
    );

    Mat4 mvp=mul(vp,model);

    glUseProgram(program);

    glUniformMatrix4fv(
        glGetUniformLocation(program,"uMVP"),
        1,GL_FALSE,mvp.m
    );

    glUniformMatrix4fv(
        glGetUniformLocation(program,"uModel"),
        1,GL_FALSE,model.m
    );

    glUniform3f(
        glGetUniformLocation(program,"uColor"),
        color.x,color.y,color.z
    );

    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES,0,36);
}

void fighter(Mat4 vp,float x,bool enemy,float t) {
    Vec3 body=enemy
        ? Vec3{0.75f,0.10f,0.18f}
        : Vec3{0.10f,0.30f,0.95f};

    Vec3 skin={1.0f,0.72f,0.48f};

    Vec3 hair=enemy
        ? Vec3{0.10f,0.04f,0.08f}
        : Vec3{0.05f,0.03f,0.12f};

    draw(vp,x-0.20f,0.48f,0,
        0.20f,0.85f,0.24f,body);

    draw(vp,x+0.20f,0.48f,0,
        0.20f,0.85f,0.24f,body);

    draw(vp,x,1.35f,0,
        0.72f,0.90f,0.40f,body);

    draw(vp,x,2.15f,0,
        0.48f,0.55f,0.43f,skin);

    draw(vp,x,2.47f,0,
        0.58f,0.24f,0.52f,hair);

    draw(vp,x-0.18f,2.37f,0,
        0.20f,0.28f,0.20f,hair);

    draw(vp,x+0.18f,2.37f,0,
        0.20f,0.28f,0.20f,hair);

    float arm=(!enemy && attackAnim>0)?0.45f:0;

    draw(vp,x-0.48f,1.40f,0,
        0.20f,0.72f,0.23f,skin);

    draw(vp,x+0.48f+arm,1.40f,0,
        0.20f,0.72f,0.23f,skin);
}

void healthBar(Mat4 vp,float x,float y,float value,Vec3 color) {
    draw(vp,x,y,-0.2f,2.1f,0.12f,0.05f,
        {0.10f,0.10f,0.10f});

    if(value>0) {
        draw(vp,x-1.05f+value*1.05f,y,0,
            2.1f*value,0.10f,0.07f,color);
    }
}

void render(float t) {
    glViewport(0,0,W,H);

    glClear(
        GL_COLOR_BUFFER_BIT|
        GL_DEPTH_BUFFER_BIT
    );

    Mat4 proj=perspective(
        0.90f,(float)W/H,0.1f,100
    );

    Mat4 vp=mul(proj,camera());

    draw(vp,0,-0.12f,0,
        12,0.2f,10,
        {0.24f,0.28f,0.34f});

    draw(vp,0,0.001f,0,
        5.5f,0.012f,0.06f,
        {0.9f,0.9f,0.9f});

    fighter(vp,playerX,false,t);
    fighter(vp,enemyX,true,t);

    healthBar(vp,-2.7f,3.5f,
        playerHP/100.0f,
        {0.05f,0.9f,0.2f});

    healthBar(vp,2.7f,3.5f,
        enemyHP/100.0f,
        {0.95f,0.05f,0.05f});
}

void update(float dt) {
    dt=std::clamp(dt,0.0f,0.05f);

    if(playerHP<=0 || enemyHP<=0) {
        moveInput=0;
        attackHeld=false;
        return;
    }

    playerX=std::clamp(
        playerX+moveInput*3.0f*dt,
        -4.0f,4.0f
    );

    attackTimer-=dt;
    enemyTimer-=dt;
    attackAnim=std::max(0.0f,attackAnim-dt);

    if(attackHeld && attackTimer<=0) {
        attackTimer=0.45f;
        attackAnim=0.22f;

        if(std::fabs(playerX-enemyX)<1.7f)
            enemyHP=std::max(0.0f,enemyHP-10.0f);
    }

    if(std::fabs(playerX-enemyX)>1.2f) {
        enemyX+=
            (playerX>enemyX?1.0f:-1.0f)*dt*1.1f;
    }
    else if(enemyTimer<=0) {
        enemyTimer=1.2f;
        playerHP=std::max(0.0f,playerHP-5.0f);
    }
}

int32_t input(android_app* app,AInputEvent* e) {
    (void)app;

    if(AInputEvent_getType(e)!=AINPUT_EVENT_TYPE_MOTION)
        return 0;

    int action=AMotionEvent_getAction(e);
    int type=action&AMOTION_EVENT_ACTION_MASK;

    int index=(action&AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
        >>AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

    if(type==AMOTION_EVENT_ACTION_CANCEL) {
        movePointer=-1;
        attackPointer=-1;
        moveInput=0;
        attackHeld=false;
        return 1;
    }

    if(type==AMOTION_EVENT_ACTION_DOWN ||
       type==AMOTION_EVENT_ACTION_POINTER_DOWN) {

        int id=AMotionEvent_getPointerId(e,index);
        float x=AMotionEvent_getX(e,index);

        if(x<W*0.48f && movePointer==-1) {
            movePointer=id;
            moveInput=x<W*0.24f?-1.0f:1.0f;
        }
        else if(x>=W*0.48f && attackPointer==-1) {
            attackPointer=id;
            attackHeld=true;
        }

        return 1;
    }

    if(type==AMOTION_EVENT_ACTION_MOVE) {
        size_t count=AMotionEvent_getPointerCount(e);

        for(size_t i=0;i<count;i++) {
            int id=AMotionEvent_getPointerId(e,i);
            float x=AMotionEvent_getX(e,i);

            if(id==movePointer) {
                moveInput=x<W*0.24f?-1.0f:
                          x<W*0.48f?1.0f:0.0f;
            }
        }

        return 1;
    }

    if(type==AMOTION_EVENT_ACTION_UP ||
       type==AMOTION_EVENT_ACTION_POINTER_UP) {

        int id=AMotionEvent_getPointerId(e,index);

        if(id==movePointer) {
            movePointer=-1;
            moveInput=0;
        }

        if(id==attackPointer) {
            attackPointer=-1;
            attackHeld=false;
        }

        return 1;
    }

    return 0;
}

void shutdownEGL() {
    if(display!=EGL_NO_DISPLAY) {
        eglMakeCurrent(
            display,EGL_NO_SURFACE,
            EGL_NO_SURFACE,EGL_NO_CONTEXT
        );

        if(context!=EGL_NO_CONTEXT) {
            if(program) glDeleteProgram(program);
            if(vbo) glDeleteBuffers(1,&vbo);
            if(vao) glDeleteVertexArrays(1,&vao);

            eglDestroyContext(display,context);
        }

        if(surface!=EGL_NO_SURFACE)
            eglDestroySurface(display,surface);

        eglTerminate(display);
    }

    display=EGL_NO_DISPLAY;
    surface=EGL_NO_SURFACE;
    context=EGL_NO_CONTEXT;

    program=vao=vbo=0;
}

void initEGL(android_app* app) {
    display=eglGetDisplay(EGL_DEFAULT_DISPLAY);

    if(display==EGL_NO_DISPLAY)
        return;

    if(eglInitialize(display,nullptr,nullptr)!=EGL_TRUE) {
        display=EGL_NO_DISPLAY;
        return;
    }

    const EGLint attrs[]={
        EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE,EGL_WINDOW_BIT,
        EGL_RED_SIZE,8,
        EGL_GREEN_SIZE,8,
        EGL_BLUE_SIZE,8,
        EGL_DEPTH_SIZE,16,
        EGL_NONE
    };

    EGLConfig config=nullptr;
    EGLint count=0;

    if(eglChooseConfig(display,attrs,&config,1,&count)!=EGL_TRUE ||
       count<1) {
        shutdownEGL();
        return;
    }

    EGLint format=0;
    eglGetConfigAttrib(
        display,config,EGL_NATIVE_VISUAL_ID,&format
    );

    ANativeWindow_setBuffersGeometry(
        app->window,0,0,format
    );

    const EGLint contextAttrs[]={
        EGL_CONTEXT_CLIENT_VERSION,3,
        EGL_NONE
    };

    surface=eglCreateWindowSurface(
        display,config,app->window,nullptr
    );

    if(surface==EGL_NO_SURFACE) {
        shutdownEGL();
        return;
    }

    context=eglCreateContext(
        display,config,EGL_NO_CONTEXT,contextAttrs
    );

    if(context==EGL_NO_CONTEXT) {
        shutdownEGL();
        return;
    }

    if(eglMakeCurrent(
        display,surface,surface,context
    )!=EGL_TRUE) {
        shutdownEGL();
        return;
    }

    eglSwapInterval(display,1);

    eglQuerySurface(display,surface,EGL_WIDTH,&W);
    eglQuerySurface(display,surface,EGL_HEIGHT,&H);

    initGraphics();
}

void command(android_app* app,int32_t cmd) {
    if(cmd==APP_CMD_INIT_WINDOW && app->window)
        initEGL(app);

    if(cmd==APP_CMD_TERM_WINDOW)
        shutdownEGL();
}

void android_main(android_app* app) {
    app_dummy();

    app->onAppCmd=command;
    app->onInputEvent=input;

    while(!app->destroyRequested) {
        int events;
        android_poll_source* source;

        while(ALooper_pollOnce(
            0,nullptr,&events,
            (void**)&source)>=0) {

            if(source)
                source->process(app,source);

            if(app->destroyRequested)
                break;
        }

        if(display==EGL_NO_DISPLAY ||
           surface==EGL_NO_SURFACE)
            continue;

        timespec ts{};
        clock_gettime(CLOCK_MONOTONIC,&ts);

        double now=
            static_cast<double>(ts.tv_sec)+
            static_cast<double>(ts.tv_nsec)/1e9;

        float dt=lastTime>0?
            static_cast<float>(now-lastTime):
            1.0f/60.0f;

        lastTime=now;

        update(dt);
        render(static_cast<float>(now));

        if(eglSwapBuffers(display,surface)!=EGL_TRUE) {
            EGLint error=eglGetError();

            LOG("Swap error: %x",error);

            if(error==EGL_CONTEXT_LOST ||
               error==EGL_BAD_SURFACE) {
                shutdownEGL();
            }
        }
    }

    shutdownEGL();
}
