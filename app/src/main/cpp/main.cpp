
#include <jni.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <cmath>
#include <vector>
#include <algorithm>
#include <cstdlib>

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

Mat4 multiply(Mat4 a, Mat4 b) {
    Mat4 r;
    for(int c=0;c<4;c++)
        for(int row=0;row<4;row++)
            for(int k=0;k<4;k++)
                r.m[c*4+row] +=
                    a.m[k*4+row]*b.m[c*4+k];
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

    // Camera position and orientation
    r.m[0]=1;
    r.m[5]=0.8944f;
    r.m[6]=-0.4472f;
    r.m[9]=0.4472f;
    r.m[10]=0.8944f;

    r.m[13]=-2.6832f;
    r.m[14]=-8.944f;

    return r;
}

const char* vertexShader = R"(
#version 300 es
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;

uniform mat4 uMVP;
uniform mat4 uModel;

out vec3 normal;

void main() {
    gl_Position = uMVP * vec4(aPos,1.0);
    normal = mat3(uModel) * aNormal;
}
)";

const char* fragmentShader = R"(
#version 300 es
precision highp float;

in vec3 normal;

uniform vec3 uColor;

out vec4 FragColor;

void main() {
    vec3 N=normalize(normal);
    vec3 L=normalize(vec3(-0.5,0.8,0.6));

    float light=max(dot(N,L),0.0);

    // Anime cel shading
    float shade;
    if(light>0.75) shade=1.0;
    else if(light>0.35) shade=0.75;
    else shade=0.42;

    FragColor=vec4(uColor*shade,1.0);
}
)";

GLuint program=0;
GLuint vao=0;
GLuint vbo=0;

EGLDisplay display;
EGLSurface surface;
EGLContext context;

int screenW=1280;
int screenH=720;

float playerX=-1.5f;
float enemyX=1.5f;

float playerHP=100;
float enemyHP=100;

float attackTimer=0;
float enemyAttackTimer=0;

bool attacking=false;
bool running=true;

float moveInput=0;

double lastTime=0;

float cube[] = {
 // Position             Normal
 -0.5,-0.5, 0.5,  0,0,1,
  0.5,-0.5, 0.5,  0,0,1,
  0.5, 0.5, 0.5,  0,0,1,
 -0.5,-0.5, 0.5,  0,0,1,
  0.5, 0.5, 0.5,  0,0,1,
 -0.5, 0.5, 0.5,  0,0,1,

 -0.5,-0.5,-0.5,  0,0,-1,
 -0.5, 0.5,-0.5,  0,0,-1,
  0.5, 0.5,-0.5,  0,0,-1,
 -0.5,-0.5,-0.5,  0,0,-1,
  0.5, 0.5,-0.5,  0,0,-1,
  0.5,-0.5,-0.5,  0,0,-1,

 -0.5,-0.5,-0.5, -1,0,0,
 -0.5,-0.5, 0.5, -1,0,0,
 -0.5, 0.5, 0.5, -1,0,0,
 -0.5,-0.5,-0.5, -1,0,0,
 -0.5, 0.5, 0.5, -1,0,0,
 -0.5, 0.5,-0.5, -1,0,0,

  0.5,-0.5,-0.5, 1,0,0,
  0.5, 0.5, 0.5, 1,0,0,
  0.5,-0.5, 0.5, 1,0,0,
  0.5,-0.5,-0.5, 1,0,0,
  0.5, 0.5,-0.5, 1,0,0,
  0.5, 0.5, 0.5, 1,0,0,

 -0.5,0.5,-0.5, 0,1,0,
 -0.5,0.5, 0.5, 0,1,0,
  0.5,0.5, 0.5, 0,1,0,
 -0.5,0.5,-0.5, 0,1,0,
  0.5,0.5, 0.5, 0,1,0,
  0.5,0.5,-0.5, 0,1,0,

 -0.5,-0.5,-0.5, 0,-1,0,
  0.5,-0.5, 0.5, 0,-1,0,
 -0.5,-0.5, 0.5, 0,-1,0,
 -0.5,-0.5,-0.5, 0,-1,0,
  0.5,-0.5,-0.5, 0,-1,0,
  0.5,-0.5, 0.5, 0,-1,0
};

GLuint compileShader(GLenum type,const char* src) {
    GLuint s=glCreateShader(type);
    glShaderSource(s,1,&src,nullptr);
    glCompileShader(s);

    GLint success=0;
    glGetShaderiv(s,GL_COMPILE_STATUS,&success);

    if(!success) {
        char log[1024];
        glGetShaderInfoLog(s,1024,nullptr,log);
        LOG("Shader error: %s",log);
    }

    return s;
}

void initGraphics() {
    GLuint vs=compileShader(
        GL_VERTEX_SHADER,vertexShader
    );

    GLuint fs=compileShader(
        GL_FRAGMENT_SHADER,fragmentShader
    );

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
    glCullFace(GL_BACK);

    glClearColor(0.40f,0.65f,0.90f,1);
}

void drawCube(
    Mat4 viewProjection,
    float x,float y,float z,
    float sx,float sy,float sz,
    Vec3 color
) {
    Mat4 model=multiply(
        translate(x,y,z),
        scale(sx,sy,sz)
    );

    Mat4 mvp=multiply(viewProjection,model);

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

void drawFighter(Mat4 vp,float x,bool enemy,float time) {
    Vec3 body=enemy
        ? Vec3{0.65f,0.12f,0.18f}
        : Vec3{0.12f,0.30f,0.95f};

    Vec3 skin={1.0f,0.72f,0.48f};
    Vec3 hair=enemy
        ? Vec3{0.12f,0.08f,0.12f}
        : Vec3{0.05f,0.04f,0.10f};

    float walk=sin(time*8.0f)*0.08f;

    // Legs
    drawCube(vp,x-0.20f,0.48f,0,
        0.20f,0.85f,0.24f,body);

    drawCube(vp,x+0.20f,0.48f,0,
        0.20f,0.85f,0.24f,body);

    // Torso
    drawCube(vp,x,1.35f,0,
        0.72f,0.90f,0.40f,body);

    // Head
    drawCube(vp,x,2.15f,0,
        0.48f,0.55f,0.43f,skin);

    // Anime hair
    drawCube(vp,x,2.47f,0,
        0.58f,0.24f,0.52f,hair);

    drawCube(vp,x-0.18f,2.37f,0,
        0.20f,0.28f,0.20f,hair);

    drawCube(vp,x+0.18f,2.37f,0,
        0.20f,0.28f,0.20f,hair);

    // Arms
    float arm=attacking && !enemy ? 0.15f : 0;

    drawCube(vp,x-0.48f,1.40f+walk,0,
        0.20f,0.72f,0.23f,skin);

    drawCube(vp,x+0.48f+arm,1.40f,0,
        0.20f,0.72f,0.23f,skin);
}

void drawBar(Mat4 vp,float x,float y,float value,Vec3 color) {
    drawCube(vp,x,y,-0.2f,
        2.1f,0.12f,0.05f,
        {0.12f,0.12f,0.12f});

    drawCube(vp,x-1.05f+value*1.05f,y,0,
        2.1f*value,0.10f,0.07f,color);
}

void renderFrame(float time) {
    glViewport(0,0,screenW,screenH);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

    float aspect=(float)screenW/(float)screenH;

    Mat4 proj=perspective(
        0.90f,aspect,0.1f,100.0f
    );

    Mat4 view=camera();
    Mat4 vp=multiply(proj,view);

    // Arena floor
    drawCube(vp,0,-0.12f,0,
        12,0.2f,10,
        {0.24f,0.28f,0.34f});

    // Arena markings
    drawCube(vp,0,0.001f,0,
        5.5f,0.012f,0.06f,
        {0.85f,0.85f,0.90f});

    drawFighter(vp,playerX,false,time);
    drawFighter(vp,enemyX,true,time);

    // Health bars
    drawBar(vp,-2.7f,3.5f,
        playerHP/100.0f,
        {0.05f,0.85f,0.25f});

    drawBar(vp,2.7f,3.5f,
        enemyHP/100.0f,
        {0.90f,0.08f,0.08f});
}

void updateGame(float dt) {
    dt=std::min(dt,0.05f);

    playerX+=moveInput*3.0f*dt;
    playerX=std::clamp(playerX,-4.0f,4.0f);

    attackTimer-=dt;
    enemyAttackTimer-=dt;

    if(attacking && attackTimer<=0) {
        attackTimer=0.45f;

        if(fabs(playerX-enemyX)<1.7f) {
            enemyHP-=10.0f;
            enemyHP=std::max(0.0f,enemyHP);
        }
    }

    attacking=false;

    if(fabs(playerX-enemyX)>1.2f) {
        enemyX+=(playerX>enemyX?1:-1)*dt*1.1f;
    }
    else if(enemyAttackTimer<=0) {
        enemyAttackTimer=1.2f;
        playerHP=std::max(0.0f,playerHP-5.0f);
    }

    if(playerHP<=0 || enemyHP<=0) {
        // Round finished
        moveInput=0;
    }
}

int32_t handleInput(
    android_app* app,AInputEvent* event
) {
    if(AInputEvent_getType(event)==AINPUT_EVENT_TYPE_MOTION) {
        int action=AMotionEvent_getAction(event);
        int index=(action&AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
            >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

        float x=AMotionEvent_getX(event,index);
        float y=AMotionEvent_getY(event,index);

        int type=action&AMOTION_EVENT_ACTION_MASK;

        if(type==AMOTION_EVENT_ACTION_UP ||
           type==AMOTION_EVENT_ACTION_CANCEL) {
            moveInput=0;
            attacking=false;
            return 1;
        }

        if(type==AMOTION_EVENT_ACTION_DOWN ||
           type==AMOTION_EVENT_ACTION_POINTER_DOWN ||
           type==AMOTION_EVENT_ACTION_MOVE) {

            if(x<screenW*0.45f) {
                moveInput=(x<screenW*0.22f)?-1.0f:1.0f;
            } else {
                attacking=true;
            }

            return 1;
        }
    }

    return 0;
}

void initEGL(android_app* app) {
    display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(display,0,0);

    const EGLint configAttribs[]={
        EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE,EGL_WINDOW_BIT,
        EGL_RED_SIZE,8,
        EGL_GREEN_SIZE,8,
        EGL_BLUE_SIZE,8,
        EGL_NONE
    };

    EGLConfig config;
    EGLint count;

    eglChooseConfig(
        display,configAttribs,
        &config,1,&count
    );

    EGLint format;
    eglGetConfigAttrib(
        display,config,EGL_NATIVE_VISUAL_ID,&format
    );

    ANativeWindow_setBuffersGeometry(
        app->window,0,0,format
    );

    const EGLint contextAttribs[]={
        EGL_CONTEXT_CLIENT_VERSION,3,
        EGL_NONE
    };

    surface=eglCreateWindowSurface(
        display,config,app->window,nullptr
    );

    context=eglCreateContext(
        display,config,EGL_NO_CONTEXT,
        contextAttribs
    );

    eglMakeCurrent(display,surface,surface,context);

    eglQuerySurface(display,surface,EGL_WIDTH,&screenW);
    eglQuerySurface(display,surface,EGL_HEIGHT,&screenH);

    initGraphics();
}

void shutdownEGL() {
    if(display!=EGL_NO_DISPLAY) {
        eglMakeCurrent(
            display,EGL_NO_SURFACE,
            EGL_NO_SURFACE,EGL_NO_CONTEXT
        );

        if(context!=EGL_NO_CONTEXT)
            eglDestroyContext(display,context);

        if(surface!=EGL_NO_SURFACE)
            eglDestroySurface(display,surface);

        eglTerminate(display);
    }

    display=EGL_NO_DISPLAY;
    context=EGL_NO_CONTEXT;
    surface=EGL_NO_SURFACE;
}

void commandHandler(
    android_app* app,int32_t command
) {
    if(command==APP_CMD_INIT_WINDOW && app->window) {
        initEGL(app);
    }

    if(command==APP_CMD_TERM_WINDOW) {
        shutdownEGL();
    }
}

void android_main(android_app* app) {
    app_dummy();

    app->onAppCmd=commandHandler;
    app->onInputEvent=handleInput;

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

        double now=ALooper_getNow();
        float dt=lastTime>0
            ? (float)(now-lastTime)/1000000000.0f
            : 0.016f;

        lastTime=now;

        updateGame(dt);
        renderFrame((float)now/1000000000.0f);

        eglSwapBuffers(display,surface);
    }

    shutdownEGL();
}
