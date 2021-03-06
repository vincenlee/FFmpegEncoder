#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavutil/log.h"

#include "mediacodec/mediacodec.h"

#ifdef ANDROID
#include <jni.h>
#include <android/log.h>
#define LOGE(format, ...)  __android_log_print(ANDROID_LOG_ERROR, "ffmpegencoder", format, ##__VA_ARGS__)
#define LOGI(format, ...)  __android_log_print(ANDROID_LOG_INFO,  "ffmpegencoder", format, ##__VA_ARGS__)
#else
#define LOGE(format, ...)  printf("(>_<) " format "\n", ##__VA_ARGS__)
#define LOGI(format, ...)  printf("(^_^) " format "\n", ##__VA_ARGS__)
#endif

#define MEDIACODEC_ENC_BUFFER_ARRAY_SIZE  1024*1024*1
#define MEDIACODEC_ENC_YUV_ARRAY_SIZE  2000*1200*3/2
#define MEDIACODEC_ANDROID  0

int encode_cancel = 0;

JNIEXPORT void JNICALL Java_com_example_ffmpegencoder_activity_MainActivity_encodeCancel
        (JNIEnv *env, jobject obj)
{
    encode_cancel = 1;
}

typedef struct MediacodecContext{
	jbyteArray enc_buffer_array;
    jbyteArray enc_yuv_array;

	jclass class_Codec;
	jclass class_HH264Encoder;
	jclass class_H264Utils;
	jclass class_Integer;
	jclass class_MediaFormat;
	jclass class_CodecCapabilities;

	jmethodID methodID_HH264Encoder_constructor;
	jmethodID methodID_HH264Encoder_config;
	jmethodID methodID_HH264Encoder_getConfig;
	jmethodID methodID_HH264Encoder_open;
	jmethodID methodID_HH264Encoder_encode;
	jmethodID methodID_HH264Encoder_getErrorCode;
	jmethodID methodID_HH264Encoder_close;
	jmethodID methodID_H264Utils_ffAvcFindStartcode;
	jmethodID methodID_Integer_intValue;
	jfieldID fieldID_Codec_ERROR_CODE_INPUT_BUFFER_FAILURE;
	jfieldID fieldID_MediaFormat_KEY_WIDTH;
	jfieldID fieldID_MediaFormat_KEY_HEIGHT;
	jfieldID fieldID_MediaFormat_KEY_COLOR_FORMAT;
	jfieldID fieldID_CodecCapabilities_COLOR_FormatYUV420Planar;
	jfieldID fieldID_CodecCapabilities_COLOR_FormatYUV420SemiPlanar;

	jint ERROR_CODE_INPUT_BUFFER_FAILURE;
	jobject KEY_WIDTH;
	jobject KEY_HEIGHT;
	jobject KEY_COLOR_FORMAT;
	jint COLOR_FormatYUV420Planar;
	jint COLOR_FormatYUV420SemiPlanar;

	jobject object_encoder;
}MediacodecContext;
void mediacodec_encode_video(JNIEnv* env, MediacodecContext* mediacodecContext, AVFrame *pFrame, AVPacket *packet, int *got_picture);
void mediacodec_encode_video2(MediaCodecEncoder* encoder, AVFrame *pFrame, AVPacket *packet, int *got_picture);

JNIEXPORT jint JNICALL Java_com_example_ffmpegencoder_activity_MainActivity_encode
		(JNIEnv *env, jobject obj, jstring input_jstr, jstring output_jstr, jint width, jint height, 
			jint framerate, jint bitrate, jint pixel_type, jint codec_type)
{
	LOGI("pixel_type:%d",pixel_type);
	LOGI("codec_type:%d",codec_type);
	jclass objcetClass = (*env)->FindClass(env,"com/example/ffmpegencoder/activity/MainActivity");
	jmethodID methodID_setProgressRate = (*env)->GetMethodID(env, objcetClass, "setProgressRate", "(I)V");
    jmethodID methodID_setProgressRateFull = (*env)->GetMethodID(env, objcetClass, "setProgressRateFull", "()V");
    jmethodID methodID_setProgressRateEmpty = (*env)->GetMethodID(env, objcetClass, "setProgressRateEmpty", "()V");
	encode_cancel = 0;
	
	AVFormatContext* pFormatCtx;
    AVOutputFormat* fmt;
    AVStream* video_st;
    AVCodecContext* pCodecCtx;
    AVCodec* pCodec;
    AVPacket pkt;
    uint8_t* pFrame_buf;
    AVFrame* pFrame;
    int picture_size;
    int y_size;
    int framecnt=0;

    char input_str[256]={0};
    char output_str[256]={0};
    char info[1024]={0};
    sprintf(input_str,"%s",(*env)->GetStringUTFChars(env,input_jstr, NULL));
    sprintf(output_str,"%s",(*env)->GetStringUTFChars(env,output_jstr, NULL));
    LOGI("Cpp input:%s",input_str);
    LOGI("Cpp output:%s",output_str);

    FILE *fp_yuv = fopen(input_str, "rb");   //Input raw YUV data

    av_register_all();
    //Method1.
    pFormatCtx = avformat_alloc_context();
    //Guess Format
    fmt = av_guess_format(NULL, output_str, NULL);
    pFormatCtx->oformat = fmt;

    //Open output URL
    if (avio_open(&pFormatCtx->pb,output_str, AVIO_FLAG_READ_WRITE) < 0){
        printf("Failed to open output file! \n");
        return -1;
    }

    video_st = avformat_new_stream(pFormatCtx, 0);
    if (video_st==NULL){
        return -1;
    }
    video_st->time_base.num = 1;
    video_st->time_base.den = framerate;

    //Param that must set
    pCodecCtx = video_st->codec;
    //pCodecCtx->codec_id =AV_CODEC_ID_HEVC;
    pCodecCtx->codec_id = fmt->video_codec;
    pCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
    pCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    //pCodecCtx->pix_fmt = AV_PIX_FMT_NV21;
    pCodecCtx->width = width;
    pCodecCtx->height = height;
    pCodecCtx->time_base.num = 1;
    pCodecCtx->time_base.den = framerate;
    pCodecCtx->bit_rate = bitrate * 1000;
    pCodecCtx->gop_size = 200;
    //H264
    //pCodecCtx->me_range = 16;
    //pCodecCtx->max_qdiff = 4;
    //pCodecCtx->qcompress = 0.6;
    pCodecCtx->qmin = 10;
    pCodecCtx->qmax = 51;

    //Optional Param
    pCodecCtx->max_b_frames=3;

    // Set Option
    AVDictionary *param = 0;
    //H.264
    if(pCodecCtx->codec_id == AV_CODEC_ID_H264) {
        av_dict_set(&param, "preset", "superfast", 0);
        av_dict_set(&param, "tune", "zerolatency", 0);
        //av_dict_set(&param, "profile", "main", 0);
    }
    //H.265
    if(pCodecCtx->codec_id == AV_CODEC_ID_H265){
        av_dict_set(&param, "preset", "ultrafast", 0);
        av_dict_set(&param, "tune", "zero-latency", 0);
    }

    //Show some Information
    av_dump_format(pFormatCtx, 0, output_str, 1);

    pCodec = avcodec_find_encoder(pCodecCtx->codec_id);
    if (!pCodec){
        LOGE("Can not find encoder! \n");
        return -1;
    }
    if (avcodec_open2(pCodecCtx, pCodec,&param) < 0){
        LOGE("Failed to open encoder! \n");
        return -1;
    }

    picture_size = avpicture_get_size(pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height);

    pFrame = av_frame_alloc();
    pFrame_buf = (uint8_t *)av_malloc(picture_size);
    avpicture_fill((AVPicture *)pFrame, pFrame_buf, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height);
	pFrame->width = pCodecCtx->width;
	pFrame->height =  pCodecCtx->height;
	
	YUV_PIXEL_FORMAT yuv_pixel_format;
	if(pixel_type == 0){
		yuv_pixel_format = I420;
	}else if(pixel_type == 5){
		yuv_pixel_format = NV12;
	}else if(pixel_type == 6){
		yuv_pixel_format = NV21;
	}

//------------Mediacodec init----------------
#if MEDIACODEC_ANDROID
	MediacodecContext mediacodecContext;

	mediacodecContext.enc_yuv_array = (*env)->NewByteArray(env, MEDIACODEC_ENC_YUV_ARRAY_SIZE);
	mediacodecContext.enc_buffer_array = (*env)->NewByteArray(env, MEDIACODEC_ENC_BUFFER_ARRAY_SIZE);

	mediacodecContext.class_Codec = (*env)->FindClass(env, "com/example/ffmpegencoder/mediacodec/Codec");
	mediacodecContext.class_HH264Encoder = (*env)->FindClass(env, "com/example/ffmpegencoder/mediacodec/HH264Encoder");
	mediacodecContext.class_H264Utils = (*env)->FindClass(env, "com/example/ffmpegencoder/mediacodec/H264Utils");
	mediacodecContext.class_Integer = (*env)->FindClass(env, "java/lang/Integer");
	mediacodecContext.class_MediaFormat = (*env)->FindClass(env,"android/media/MediaFormat");
	mediacodecContext.class_CodecCapabilities = (*env)->FindClass(env,"android/media/MediaCodecInfo$CodecCapabilities");

	mediacodecContext.methodID_HH264Encoder_constructor = (*env)->GetMethodID(env,mediacodecContext.class_HH264Encoder,"<init>","(IIII)V");
	mediacodecContext.methodID_HH264Encoder_config = (*env)->GetMethodID(env,mediacodecContext.class_HH264Encoder,"config","(Ljava/lang/String;Ljava/lang/Object;)V");
	mediacodecContext.methodID_HH264Encoder_getConfig = (*env)->GetMethodID(env,mediacodecContext.class_HH264Encoder,"getConfig","(Ljava/lang/String;)Ljava/lang/Object;");
	mediacodecContext.methodID_HH264Encoder_open = (*env)->GetMethodID(env,mediacodecContext.class_HH264Encoder,"open","()V");
	mediacodecContext.methodID_HH264Encoder_encode = (*env)->GetMethodID(env,mediacodecContext.class_HH264Encoder,"encode","([BI[BI)I");
	mediacodecContext.methodID_HH264Encoder_getErrorCode = (*env)->GetMethodID(env,mediacodecContext.class_HH264Encoder,"getErrorCode","()I");
	mediacodecContext.methodID_HH264Encoder_close = (*env)->GetMethodID(env,mediacodecContext.class_HH264Encoder,"close","()V");
	mediacodecContext.methodID_H264Utils_ffAvcFindStartcode = (*env)->GetStaticMethodID(env,mediacodecContext.class_H264Utils,"ffAvcFindStartcode","([BII)I");
	mediacodecContext.methodID_Integer_intValue = (*env)->GetMethodID(env,mediacodecContext.class_Integer,"intValue","()I");
	mediacodecContext.fieldID_Codec_ERROR_CODE_INPUT_BUFFER_FAILURE = (*env)->GetStaticFieldID(env, mediacodecContext.class_Codec, "ERROR_CODE_INPUT_BUFFER_FAILURE", "I");
	mediacodecContext.fieldID_MediaFormat_KEY_WIDTH = (*env)->GetStaticFieldID(env, mediacodecContext.class_MediaFormat, "KEY_WIDTH", "Ljava/lang/String;");
	mediacodecContext.fieldID_MediaFormat_KEY_HEIGHT = (*env)->GetStaticFieldID(env, mediacodecContext.class_MediaFormat, "KEY_HEIGHT", "Ljava/lang/String;");
	mediacodecContext.fieldID_MediaFormat_KEY_COLOR_FORMAT = (*env)->GetStaticFieldID(env, mediacodecContext.class_MediaFormat, "KEY_COLOR_FORMAT", "Ljava/lang/String;");
	mediacodecContext.fieldID_CodecCapabilities_COLOR_FormatYUV420Planar = (*env)->GetStaticFieldID(env,  mediacodecContext.class_CodecCapabilities, "COLOR_FormatYUV420Planar", "I");
	mediacodecContext.fieldID_CodecCapabilities_COLOR_FormatYUV420SemiPlanar = (*env)->GetStaticFieldID(env,  mediacodecContext.class_CodecCapabilities, "COLOR_FormatYUV420SemiPlanar", "I");

	mediacodecContext.ERROR_CODE_INPUT_BUFFER_FAILURE = (*env)->GetStaticIntField(env, mediacodecContext.class_Codec, mediacodecContext.fieldID_Codec_ERROR_CODE_INPUT_BUFFER_FAILURE);
	mediacodecContext.KEY_WIDTH = (*env)->GetStaticObjectField(env, mediacodecContext.class_MediaFormat, mediacodecContext.fieldID_MediaFormat_KEY_WIDTH);
	mediacodecContext.KEY_HEIGHT = (*env)->GetStaticObjectField(env, mediacodecContext.class_MediaFormat, mediacodecContext.fieldID_MediaFormat_KEY_HEIGHT);
	mediacodecContext.KEY_COLOR_FORMAT = (*env)->GetStaticObjectField(env, mediacodecContext.class_MediaFormat, mediacodecContext.fieldID_MediaFormat_KEY_COLOR_FORMAT);
	mediacodecContext.COLOR_FormatYUV420Planar = (*env)->GetStaticIntField(env, mediacodecContext.class_CodecCapabilities, mediacodecContext.fieldID_CodecCapabilities_COLOR_FormatYUV420Planar);
	mediacodecContext.COLOR_FormatYUV420SemiPlanar = (*env)->GetStaticIntField(env, mediacodecContext.class_CodecCapabilities, mediacodecContext.fieldID_CodecCapabilities_COLOR_FormatYUV420SemiPlanar);

	mediacodecContext.object_encoder = (*env)->NewObject(env, mediacodecContext.class_HH264Encoder, mediacodecContext.methodID_HH264Encoder_constructor,width, height, framerate, bitrate*1000);
	(*env)->CallVoidMethod(env, mediacodecContext.object_encoder, mediacodecContext.methodID_HH264Encoder_open);
#else
	MediaCodecEncoder* mediacodec_encoder = mediacodec_encoder_alloc(1,width,height,framerate,bitrate*1000,1000,yuv_pixel_format);
	mediacodec_encoder_open(mediacodec_encoder);
#endif

    //Write File Header
    avformat_write_header(pFormatCtx,NULL);

    av_new_packet(&pkt,picture_size);

    y_size = pCodecCtx->width * pCodecCtx->height;

    //Read raw YUV data
    while(fread(pFrame_buf, 1, y_size*3/2, fp_yuv) > 0 && encode_cancel == 0){
        pFrame->data[0] = pFrame_buf;              // Y
        pFrame->data[1] = pFrame_buf+ y_size;      // U
        pFrame->data[2] = pFrame_buf+ y_size*5/4;  // V
        pFrame->pts=framecnt;                       //PTS

        int got_picture=0;
        //Encode
		if(codec_type == 0){
			int ret = avcodec_encode_video2(pCodecCtx, &pkt,pFrame, &got_picture);
			if(ret < 0){
				LOGE("Failed to encode! \n");
				return -1;
			}
			if (got_picture==1){
				LOGI("Succeed to encode frame: %5d\tsize:%5d\n",framecnt,pkt.size);

				LOGI("Frame Index: %5d  size:%5d",framecnt,pkt.size);
				(*env)->CallVoidMethod(env, obj, methodID_setProgressRate, framecnt);

				framecnt++;
				pkt.stream_index = video_st->index;
				ret = av_write_frame(pFormatCtx, &pkt);
			}
		}
		else if(codec_type == 1){
			if(!pkt.data){
				av_new_packet(&pkt,picture_size);
			}
#if MEDIACODEC_ANDROID
            mediacodec_encode_video(env, &mediacodecContext, pFrame, &pkt, &got_picture);
#else
			mediacodec_encode_video2(mediacodec_encoder, pFrame, &pkt, &got_picture);
#endif			
			if (got_picture==1){
				LOGI("Succeed to encode frame: %5d\tsize:%5d\n",framecnt,pkt.size);

				LOGI("Frame Index: %5d  size:%5d",framecnt,pkt.size);
				(*env)->CallVoidMethod(env, obj, methodID_setProgressRate, framecnt);

				framecnt++;
				pkt.stream_index = video_st->index;
				av_write_frame(pFormatCtx, &pkt);
			}
		}
		av_free_packet(&pkt);
    }

    //Flush Encoder
    AVPacket enc_pkt;
    int got_frame;
    int ret;
    if (!(pCodec->capabilities & CODEC_CAP_DELAY))
        return 0;

    while (encode_cancel == 0) {
        enc_pkt.data = NULL;
        enc_pkt.size = 0;
        av_init_packet(&enc_pkt);
		if(codec_type == 0){
			ret = avcodec_encode_video2 (pCodecCtx, &enc_pkt, NULL, &got_frame);
			av_frame_free(NULL);
			if (ret < 0){
				LOGE("Flushing encoder failed\n");
				break;
			}
			if (!got_frame){
				ret=0;
				break;
			}
			LOGI("Flush Encoder-Frame Index: %5d  size:%5d",framecnt,enc_pkt.size);
			(*env)->CallVoidMethod(env, obj, methodID_setProgressRate, framecnt);

			framecnt++;
			/* mux encoded frame */
			ret = av_write_frame(pFormatCtx, &enc_pkt);
		}
		else if(codec_type == 1){
			break;
		}
    }

    if(encode_cancel == 0){
        LOGI("Encoder Finish!!!");
        (*env)->CallVoidMethod(env, obj, methodID_setProgressRateFull);
    }
    else{
        LOGI("Encoder Cancel!!!");
        (*env)->CallVoidMethod(env, obj, methodID_setProgressRateEmpty);
    }
	
    //Write file trailer
    av_write_trailer(pFormatCtx);
    //Clean
    if (video_st){
        avcodec_close(video_st->codec);
        av_free(pFrame);
        av_free(pFrame_buf);
    }
    avio_close(pFormatCtx->pb);
    avformat_free_context(pFormatCtx);

    fclose(fp_yuv);
    encode_cancel = 0;
	
#if MEDIACODEC_ANDROID
	(*env)->DeleteLocalRef(env, mediacodecContext.enc_yuv_array);
	(*env)->DeleteLocalRef(env, mediacodecContext.enc_buffer_array);

	(*env)->DeleteLocalRef(env, mediacodecContext.class_Codec);
	(*env)->DeleteLocalRef(env, mediacodecContext.class_HH264Encoder);
	(*env)->DeleteLocalRef(env, mediacodecContext.class_H264Utils);
	(*env)->DeleteLocalRef(env, mediacodecContext.class_Integer);
	(*env)->DeleteLocalRef(env, mediacodecContext.class_MediaFormat);
	(*env)->DeleteLocalRef(env, mediacodecContext.class_CodecCapabilities);

	(*env)->CallVoidMethod(env, mediacodecContext.object_encoder, mediacodecContext.methodID_HH264Encoder_close);
	(*env)->DeleteLocalRef(env, mediacodecContext.object_encoder);
	(*env)->DeleteLocalRef(env, mediacodecContext.KEY_WIDTH);
	(*env)->DeleteLocalRef(env, mediacodecContext.KEY_HEIGHT);
	(*env)->DeleteLocalRef(env, mediacodecContext.KEY_COLOR_FORMAT);
#else
	mediacodec_encoder_close(mediacodec_encoder);
	mediacodec_encoder_free(mediacodec_encoder);
#endif
    return 0;
}

void mediacodec_encode_video(JNIEnv* env, MediacodecContext* mediacodecContext, AVFrame *pFrame, AVPacket *packet, int *got_picture){
	uint8_t *in,*out;
	int in_len = pFrame->width * pFrame->height * 3 / 2;
	int out_len = 0;
	in = pFrame->data[0];
	out = packet->data;
	
	int jerror_code;
	
	(*env)->SetByteArrayRegion(env, mediacodecContext->enc_yuv_array, 0, in_len, (jbyte*)in);
	
	out_len = (*env)->CallIntMethod(env,mediacodecContext->object_encoder,mediacodecContext->methodID_HH264Encoder_encode,mediacodecContext->enc_yuv_array,0,mediacodecContext->enc_buffer_array,in_len);
	
	LOGI("out_len:%6d", out_len);
	if(out_len > 0){	
		(*env)->GetByteArrayRegion(env, mediacodecContext->enc_buffer_array, 0, out_len, (jbyte*)out);
	
		*got_picture = 1;
		packet->size = out_len;
	}
	else{
		*got_picture = 0;
		packet->size = 0;
	}
}

void mediacodec_encode_video2(MediaCodecEncoder* encoder, AVFrame *pFrame, AVPacket *packet, int *got_picture){
	uint8_t *in,*out;
	int in_len = pFrame->width * pFrame->height * 3 / 2;
	int out_len = 0;
	in = pFrame->data[0];
	out = packet->data;
	
	int jerror_code;
	
	out_len = mediacodec_encoder_encode(encoder, in, 0, out, in_len, &jerror_code);
	LOGI("out_len:%6d", out_len);
	if(out_len > 0){	
		*got_picture = 1;
		packet->size = out_len;
	}
	else{
		*got_picture = 0;
		packet->size = 0;
	}
}

#define MEDIACODEC_BUFFER_SIZE  1024*1024*1
#define ENCODER_FRAMERATE 25
#define ENCODER_BITRATE 10000000

JNIEXPORT jint JNICALL Java_com_example_ffmpegencoder_activity_MainActivity_MediaEncode
		(JNIEnv *env, jobject obj, jstring input_jstr, jstring output_jstr, jint width, jint height)
{
	jclass objcetClass = (*env)->FindClass(env,"com/example/ffmpegencoder/activity/MainActivity");
	jmethodID methodID_setProgressRate = (*env)->GetMethodID(env, objcetClass, "setProgressRate", "(I)V");
	jmethodID methodID_setProgressRateFull = (*env)->GetMethodID(env, objcetClass, "setProgressRateFull", "()V");
	jmethodID methodID_setProgressRateEmpty = (*env)->GetMethodID(env, objcetClass, "setProgressRateEmpty", "()V");
	encode_cancel = 0;
	
	jclass class_HH264Encoder = (*env)->FindClass(env,"com/example/ffmpegencoder/mediacodec/HH264Encoder");
	jmethodID methodID_HH264Encoder_constructor = (*env)->GetMethodID(env,class_HH264Encoder,"<init>","(IIII)V");
	jmethodID methodID_HH264Encoder_open = (*env)->GetMethodID(env,class_HH264Encoder,"open","()V");
	jmethodID methodID_HH264Encoder_encode = (*env)->GetMethodID(env,class_HH264Encoder,"encode","([BI[BI)I");
	jmethodID methodID_HH264Encoder_close = (*env)->GetMethodID(env,class_HH264Encoder,"close","()V");

	int bit_rate = width * height * 3 / 2 * ENCODER_FRAMERATE * 8 / 100;
	LOGI("bit_rate:%d",bit_rate);
	jobject object_encoder = (*env)->NewObject(env,class_HH264Encoder,methodID_HH264Encoder_constructor, 
		width, height, ENCODER_FRAMERATE, bit_rate);
	(*env)->CallVoidMethod(env,object_encoder,methodID_HH264Encoder_open);
		
	char input_str[256]={0};
	char output_str[256]={0};
	sprintf(input_str,"%s",(*env)->GetStringUTFChars(env,input_jstr, NULL));
	sprintf(output_str,"%s",(*env)->GetStringUTFChars(env,output_jstr, NULL));
	LOGI("Cpp input:%s",input_str);
	LOGI("Cpp output:%s",output_str);
	
	FILE *fileIn = fopen(input_str,"rb");
	FILE *fileOut = fopen(output_str,"wb");
	if(fileIn == NULL || fileOut == NULL)
	{
		LOGE("open err!!!!");
		return -1;
	}
	char *yuv = (char*)malloc(sizeof(char) * width * height * 3 / 2);
	char *buffer = (char*)malloc(sizeof(char) * MEDIACODEC_BUFFER_SIZE);
	memset(buffer,0,sizeof(char) * MEDIACODEC_BUFFER_SIZE);
	long frame = 0;
	jbyteArray jyuvArray = (*env)->NewByteArray(env,width * height * 3 / 2);
	jbyteArray jbufferArray = (*env)->NewByteArray(env,MEDIACODEC_BUFFER_SIZE);
	jint jout_len;
		 
	struct timeval start,end,old,new;
	int first = 1;
	gettimeofday(&start,NULL);
	int read_len;
	while((read_len = fread(yuv, sizeof(char), width * height * 3 / 2 ,fileIn)) == width * height * 3 / 2 && !encode_cancel){
		LOGI("%d====%d",read_len,encode_cancel);
		(*env)->SetByteArrayRegion(env,jyuvArray, 0, width * height * 3 / 2, (jbyte*)yuv);
		if(first == 1){
			gettimeofday(&old,NULL);
			first = 0;
		}
		else{
			gettimeofday(&new,NULL);
			while(new.tv_sec * 1000000 + new.tv_usec - old.tv_sec * 1000000 - old.tv_usec < 40 * 1000){
				LOGI("===duration:%ld", new.tv_sec * 1000000 + new.tv_usec - old.tv_sec * 1000000 - old.tv_usec);
				usleep(1000);
				gettimeofday(&new,NULL);
			}
			LOGI("duration:%ld ", new.tv_sec * 1000000 + new.tv_usec - old.tv_sec * 1000000 - old.tv_usec);
			
			old.tv_sec = new.tv_sec;
			old.tv_usec = new.tv_usec;
		}
		
		jout_len = (*env)->CallIntMethod(env, object_encoder, methodID_HH264Encoder_encode, jyuvArray, 0, jbufferArray, width * height * 3 / 2);

		LOGI("jout_len:%6d", jout_len);
	
		if(jout_len > 0){	
			LOGI("frame:%6ld", frame);
			(*env)->CallVoidMethod(env, obj, methodID_setProgressRate, frame);
			frame++;
			
			(*env)->GetByteArrayRegion(env, jbufferArray, 0, jout_len, (jbyte*)buffer);
			fwrite(buffer,1,jout_len,fileOut);
		}
	}
	LOGI("%d####%d",read_len,encode_cancel);
	gettimeofday(&end,NULL);
	LOGI("time_duration:%lf", ((double)(end.tv_sec * 1000000 + end.tv_usec - start.tv_sec * 1000000 - start.tv_usec)) / 1000000);
	LOGI("avg_frame_rate:%lf", (double)frame * 1000000 / ((double)(end.tv_sec * 1000000 + end.tv_usec - start.tv_sec * 1000000 - start.tv_usec)));
	
	if(encode_cancel == 0){
		LOGI("Encoder Finish!!!");
		(*env)->CallVoidMethod(env, obj, methodID_setProgressRateFull);
	}
	else{
		LOGI("Encoder Cancel!!!");
		(*env)->CallVoidMethod(env, obj, methodID_setProgressRateEmpty);
	}
	
	fclose(fileIn);
	fclose(fileOut);
	free(buffer);
	free(yuv);
	(*env)->CallVoidMethod(env,object_encoder,methodID_HH264Encoder_close);
	encode_cancel = 0;
	return 0;
}
