#include "video_filter.h"

// 创建配置一个滤镜图，在后续滤镜处理中，可以往此滤镜图输入数据并从滤镜图获得输出数据
int init_video_filters(const char *filters_descr, const input_vfmt_t *vfmt, filter_ctx_t *fctx)
{
    int ret = 0;

    // 分配一个滤镜图filter_graph
    fctx->filter_graph = avfilter_graph_alloc();
    if (!fctx->filter_graph)
    {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    char args[512];
    char *p_args = NULL;
    if (vfmt != NULL)
    {
        /* buffer video source: the decoded frames from the decoder will be inserted here. */
        // args是buffersrc滤镜的参数
        snprintf(args, sizeof(args),
                 "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
                 vfmt->width, vfmt->height, vfmt->pix_fmt, 
                 vfmt->time_base.num, vfmt->time_base.den, 
                 vfmt->sar.num, vfmt->sar.den);
        p_args = args;
    }

    // buffer滤镜：缓冲视频帧，作为滤镜图的输入
    const AVFilter *bufsrc  = avfilter_get_by_name("buffer");
    // 为buffersrc滤镜创建滤镜实例buffersrc_ctx，命名为"in"
    // 将新创建的滤镜实例buffersrc_ctx添加到滤镜图filter_graph中
    ret = avfilter_graph_create_filter(&fctx->bufsrc_ctx, bufsrc, "in",
                                       p_args, NULL, fctx->filter_graph);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
        goto end;
    }

    // buffersink滤镜：缓冲视频帧，作为滤镜图的输出
    const AVFilter *bufsink = avfilter_get_by_name("buffersink");
    /* buffer video sink: to terminate the filter chain. */
    // 为buffersink滤镜创建滤镜实例buffersink_ctx，命名为"out"
    // 将新创建的滤镜实例buffersink_ctx添加到滤镜图filter_graph中
    ret = avfilter_graph_create_filter(&fctx->bufsink_ctx, bufsink, "out",
                                       NULL, NULL, fctx->filter_graph);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
        goto end;
    }

#if 0   // 因为后面显示视频帧时有sws_scale()进行图像格式转换，故此处不设置滤镜输出格式也可
    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUYV422, AV_PIX_FMT_NONE };
    // 设置输出像素格式为pix_fmts[]中指定的格式(如果要用SDL显示，则这些格式应是SDL支持格式)
    ret = av_opt_set_int_list(buffersink_ctx, "pix_fmts", pix_fmts,
                              AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Cannot set output pixel format\n");
        goto end;
    }
#endif

    /*
     * Set the endpoints for the filter graph. The filter_graph will
     * be linked to the graph described by filters_descr.
     */
    // 设置滤镜图的端点，将filters_descr描述的滤镜图连接到此滤镜图，
    // 两个滤镜图的连接是通过端点连接(AVFilterInOut)完成的

    /*
     * The buffer source output must be connected to the input pad of
     * the first filter described by filters_descr; since the first
     * filter input label is not specified, it is set to "in" by
     * default.
     */
    // outputs变量意指buffersrc_ctx滤镜的输出引脚(output pad)
    // src缓冲区(buffersrc_ctx滤镜)的输出必须连到filters_descr中第一个
    // 滤镜的输入；filters_descr中第一个滤镜的输入标号未指定，故默认为
    // "in"，此处将buffersrc_ctx的输出标号也设为"in"，就实现了同标号相连
    AVFilterInOut *outputs = avfilter_inout_alloc();
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = fctx->bufsrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;

    /*
     * The buffer sink input must be connected to the output pad of
     * the last filter described by filters_descr; since the last
     * filter output label is not specified, it is set to "out" by
     * default.
     */
    // inputs变量意指buffersink_ctx滤镜的输入引脚(input pad)
    // sink缓冲区(buffersink_ctx滤镜)的输入必须连到filters_descr中最后
    // 一个滤镜的输出；filters_descr中最后一个滤镜的输出标号未指定，故
    // 默认为"out"，此处将buffersink_ctx的输出标号也设为"out"，就实现了
    // 同标号相连
    AVFilterInOut *inputs  = avfilter_inout_alloc();
    inputs->name       = av_strdup("out");
    inputs->filter_ctx = fctx->bufsink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;

    // 将filters_descr描述的滤镜图添加到filter_graph滤镜图中
    // 调用前：filter_graph包含两个滤镜buffersrc_ctx和buffersink_ctx
    // 调用后：filters_descr描述的滤镜图插入到filter_graph中，buffersrc_ctx连接到filters_descr
    //         的输入，filters_descr的输出连接到buffersink_ctx，filters_descr只进行了解析而不
    //         建立内部滤镜间的连接。filters_desc与filter_graph间的连接是利用AVFilterInOut inputs
    //         和AVFilterInOut outputs连接起来的，AVFilterInOut是一个链表，最终可用的连在一起的
    //         滤镜链/滤镜图就是通过这个链表串在一起的。
    ret = avfilter_graph_parse_ptr(fctx->filter_graph, filters_descr,
                                   &inputs, &outputs, NULL);
    if (ret < 0)
    {
        goto end;
    }

    // 验证有效性并配置filtergraph中所有连接和格式
    ret = avfilter_graph_config(fctx->filter_graph, NULL);
    if (ret < 0)
    {
        goto end;
    }

end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}

int deinit_filters(filter_ctx_t *fctx)
{
    avfilter_graph_free(&fctx->filter_graph);

    return 0;
}

int filtering_video_frame(const filter_ctx_t *fctx, AVFrame *frame_in, AVFrame *frame_out)
{
    int ret;
    
    // 将frame送入filtergraph
    ret = av_buffersrc_add_frame_flags(fctx->bufsrc_ctx, frame_in, AV_BUFFERSRC_FLAG_KEEP_REF);
    if (ret < 0)
    {
        av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
        return ret;
    }
    
    // 从filtergraph获取经过处理的frame
    ret = av_buffersink_get_frame(fctx->bufsink_ctx, frame_out);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
    {
        av_log(NULL, AV_LOG_WARNING, "Need more frames\n");
        return 0;
    }
    else if (ret < 0)
    {
        return ret;
    }

    return 1;
}


