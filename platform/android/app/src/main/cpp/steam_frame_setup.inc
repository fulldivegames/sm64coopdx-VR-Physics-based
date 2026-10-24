/* Included only for SM64_STEAM_FRAME. Setup renders without a game or Android
 * surface: Lepton normally hides Android windows for VR APKs. */
#define SETUP_WIDTH 1024
#define SETUP_HEIGHT 768
static pthread_mutex_t sSetupMutex = PTHREAD_MUTEX_INITIALIZER;
static uint32_t sSetupPixels[SETUP_WIDTH * SETUP_HEIGHT];
static int sSetupGeneration;
static int sSetupUploadedGeneration;
static XrSwapchain sSetupSwapchain;
static XrSwapchainImageOpenGLESKHR *sSetupImages;
static uint32_t sSetupImageCount;
static int *sSetupImageGenerations;
static XrSpace sSetupViewSpace;
static bool sSteamFrameGameStarted;

JNIEXPORT void JNICALL
Java_com_fulldivegames_sm64coopdxvr_QuestNativeActivity_nativeSetupPixels(
        JNIEnv *env, jclass clazz, jintArray pixels, jint generation) {
    (void)clazz;
    if (!pixels || (*env)->GetArrayLength(env, pixels) != SETUP_WIDTH * SETUP_HEIGHT) return;
    jint *source = (*env)->GetIntArrayElements(env, pixels, NULL);
    if (!source) return;
    pthread_mutex_lock(&sSetupMutex);
    for (size_t i = 0; i < SETUP_WIDTH * SETUP_HEIGHT; ++i) {
        uint32_t c = (uint32_t)source[i];
        // Java ARGB integers -> RGBA bytes on little-endian ARM64.
        size_t destination = (SETUP_HEIGHT - 1 - i / SETUP_WIDTH) * SETUP_WIDTH + i % SETUP_WIDTH;
        sSetupPixels[destination] = (c & 0xff00ff00U) | ((c & 0xffU) << 16) | ((c >> 16) & 0xffU);
    }
    sSetupGeneration = generation;
    pthread_mutex_unlock(&sSetupMutex);
    (*env)->ReleaseIntArrayElements(env, pixels, source, JNI_ABORT);
}

static void setup_input(QuestApp *app) {
    int direction; bool select;
    if (app->session_state != XR_SESSION_STATE_FOCUSED ||
        !quest_input_setup_navigation(app->session, &direction, &select) || (!direction && !select)) return;
    JNIEnv *env = NULL;
    JavaVM *vm = app->android_app->activity->vm;
    bool attached = (*vm)->GetEnv(vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK;
    if (attached && (*vm)->AttachCurrentThread(vm, &env, NULL) != JNI_OK) return;
    jobject activity = app->android_app->activity->clazz;
    jclass clazz = (*env)->GetObjectClass(env, activity);
    jmethodID method = (*env)->GetMethodID(env, clazz, "onSteamFrameSetupInput", "(IZI)V");
    if (method) (*env)->CallVoidMethod(env, activity, method, direction, select, sSetupUploadedGeneration);
    if ((*env)->ExceptionCheck(env)) { (*env)->ExceptionDescribe(env); (*env)->ExceptionClear(env); }
    (*env)->DeleteLocalRef(env, clazz);
    if (attached) (*vm)->DetachCurrentThread(vm);
}

static bool create_setup_panel(QuestApp *app) {
    XrReferenceSpaceCreateInfo space = {.type=XR_TYPE_REFERENCE_SPACE_CREATE_INFO,
        .referenceSpaceType=XR_REFERENCE_SPACE_TYPE_VIEW,.poseInReferenceSpace={.orientation={.w=1}}};
    if (!xr_check("xrCreateReferenceSpace(setup)", xrCreateReferenceSpace(app->session, &space, &sSetupViewSpace))) return false;
    XrSwapchainCreateInfo info = {.type=XR_TYPE_SWAPCHAIN_CREATE_INFO,
        .usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT,
        .format=select_swapchain_format(app),.sampleCount=1,.width=SETUP_WIDTH,.height=SETUP_HEIGHT,
        .faceCount=1,.arraySize=1,.mipCount=1};
    if (!info.format || !xr_check("xrCreateSwapchain(setup)", xrCreateSwapchain(app->session,&info,&sSetupSwapchain))) return false;
    if (!xr_check("xrEnumerateSwapchainImages(setup count)", xrEnumerateSwapchainImages(sSetupSwapchain,0,&sSetupImageCount,NULL)) || !sSetupImageCount) return false;
    sSetupImages = calloc(sSetupImageCount,sizeof(*sSetupImages));
    sSetupImageGenerations = malloc(sSetupImageCount*sizeof(*sSetupImageGenerations));
    if (!sSetupImages || !sSetupImageGenerations) return false;
    for (uint32_t i=0;i<sSetupImageCount;++i) {
        sSetupImages[i].type=XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
        sSetupImageGenerations[i]=-1;
    }
    return xr_check("xrEnumerateSwapchainImages(setup)",xrEnumerateSwapchainImages(sSetupSwapchain,sSetupImageCount,&sSetupImageCount,(XrSwapchainImageBaseHeader*)sSetupImages));
}

static bool render_setup_frame(QuestApp *app) {
    XrFrameWaitInfo wait={.type=XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState state={.type=XR_TYPE_FRAME_STATE};
    if (!xr_check("xrWaitFrame(setup)",xrWaitFrame(app->session,&wait,&state))) return false;
    XrFrameBeginInfo begin={.type=XR_TYPE_FRAME_BEGIN_INFO};
    if (!xr_check("xrBeginFrame(setup)",xrBeginFrame(app->session,&begin))) return false;
    bool success=true;
    if (state.shouldRender) {
        setup_input(app);
        uint32_t index=0;
        XrSwapchainImageAcquireInfo acquire={.type=XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        XrSwapchainImageWaitInfo image_wait={.type=XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO,.timeout=XR_INFINITE_DURATION};
        success=xr_check("xrAcquireSwapchainImage(setup)",xrAcquireSwapchainImage(sSetupSwapchain,&acquire,&index));
        if (success) {
            success=xr_check("xrWaitSwapchainImage(setup)",xrWaitSwapchainImage(sSetupSwapchain,&image_wait));
            if (success && index<sSetupImageCount) {
                glBindTexture(GL_TEXTURE_2D,sSetupImages[index].image);
                pthread_mutex_lock(&sSetupMutex);
                if (sSetupImageGenerations[index] != sSetupGeneration) {
                    glTexSubImage2D(GL_TEXTURE_2D,0,0,0,SETUP_WIDTH,SETUP_HEIGHT,GL_RGBA,GL_UNSIGNED_BYTE,sSetupPixels);
                    sSetupImageGenerations[index]=sSetupGeneration;
                }
                sSetupUploadedGeneration=sSetupGeneration;
                pthread_mutex_unlock(&sSetupMutex);
                success=glGetError()==GL_NO_ERROR;
                glFlush();
                XrSwapchainImageReleaseInfo release={.type=XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                success=xr_check("xrReleaseSwapchainImage(setup)",xrReleaseSwapchainImage(sSetupSwapchain,&release)) && success;
            }
        }
    }
    XrCompositionLayerQuad quad={.type=XR_TYPE_COMPOSITION_LAYER_QUAD,.space=sSetupViewSpace,
        .eyeVisibility=XR_EYE_VISIBILITY_BOTH,.subImage={.swapchain=sSetupSwapchain,
            .imageRect={.extent={SETUP_WIDTH,SETUP_HEIGHT}}},
        .pose={.orientation={.w=1},.position={.z=-1.6f}},.size={1.4f,1.05f}};
    const XrCompositionLayerBaseHeader *layer=(const XrCompositionLayerBaseHeader*)&quad;
    XrFrameEndInfo end={.type=XR_TYPE_FRAME_END_INFO,.displayTime=state.predictedDisplayTime,
        .environmentBlendMode=app->blend_mode,.layerCount=state.shouldRender && success ? 1 : 0,.layers=&layer};
    return xr_check("xrEndFrame(setup)",xrEndFrame(app->session,&end)) && success;
}

static bool poll_setup_rom(QuestApp *app) {
    if (sSteamFrameGameStarted) return true;
    static int accepted_attempt;
    int attempt=0;
    char root[sizeof(sSteamFrameDataRoot)];
    pthread_mutex_lock(&sSteamFrameRomMutex);
    attempt=atomic_exchange_explicit(&sSteamFrameRomAttempt,0,memory_order_acq_rel);
    memcpy(root,sSteamFrameDataRoot,sizeof(root));
    pthread_mutex_unlock(&sSteamFrameRomMutex);
    if (attempt>0) {
        char path[512]; snprintf(path,sizeof(path),"%s/baserom.us.z64",root);
        app->rom=quest_rom_validate_file(root[0]?path:NULL);
        bool valid=app->rom.status==QUEST_ROM_VALID;
        if (valid) quest_android_set_user_path(root);
        if (!steam_frame_report_rom_result(attempt,valid,valid?"":"The selected ROM could not be validated.")) return false;
        accepted_attempt=valid?attempt:0;
    }
    if (accepted_attempt>0 && atomic_load_explicit(&sSteamFrameRomConfirmed,memory_order_acquire)==accepted_attempt) {
        quest_game_load_early_config();
        if (!create_mario_preview(app) || !create_swapchains(app) || !quest_game_initialize()) return false;
        configure_display_refresh_rate(app);
        sSteamFrameGameStarted=true;
        LOGI("Steam Frame: headset ROM setup completed; entering gameplay.");
    }
    return true;
}

static void destroy_setup_panel(void) {
    if (sSetupSwapchain) xrDestroySwapchain(sSetupSwapchain);
    if (sSetupViewSpace) xrDestroySpace(sSetupViewSpace);
    free(sSetupImages); sSetupImages=NULL;
    free(sSetupImageGenerations); sSetupImageGenerations=NULL;
    sSetupSwapchain=XR_NULL_HANDLE; sSetupViewSpace=XR_NULL_HANDLE;
}
