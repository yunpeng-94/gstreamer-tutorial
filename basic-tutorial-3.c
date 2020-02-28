#include <gst/gst.h>

/* Structure to contain all our information, so we can pass it to callbacks */
typedef struct _CustomData
{
  GstElement *pipeline;
  GstElement *source;
  // audio
  GstElement *convert;
  GstElement *resample;
  GstElement *sink;
  // video
  GstElement *videosink;
} CustomData;

/* Handler for the pad-added signal */
static void pad_added_handler (GstElement * src, GstPad * pad,
    CustomData * data);

int
main (int argc, char *argv[])
{
  CustomData data;
  GstBus *bus;
  GstMessage *msg;
  GstStateChangeReturn ret;
  gboolean terminate = FALSE;

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  /* Create the elements */
  // uridecodebin将在内部实例化所有必要的元素(sources、demuxers和decoders)，以将URI转换为原始音频和/或视频流。
  // 它做了playbin一半的工作。因为它包含了demuxers，所以它的source pad最初是不可用的，我们需要动态地链接到它们。
  // 在处理demuxers时，复杂点在于demuxer在接收到一些数据并有机会查看这个容器和其中的内容之前，它们无法生成任何信息。
  // 也就是说，demuxers一开始并没有source pads，因为它还不知道容器中的内容，其他elements也就无法与之相关联，因此pipeline必须在demuxers处终止。
  data.source = gst_element_factory_make ("uridecodebin", "source");
  // audioconvert对于在不同的音频格式之间进行转换非常有用，因为音频解码器生成的格式可能与音频接收器期望的格式不同，所以要确保这个示例可以在任何平台上工作
  data.convert = gst_element_factory_make ("audioconvert", "convert");
  // audioresample对于不同音频采样率之间的转换非常有用，同样可以确保此示例在任何平台上都能工作，因为音频解码器生成的音频采样率可能不是音频接收器支持的音频采样率。
  data.resample = gst_element_factory_make ("audioresample", "resample");
  // autoaudiosink用于将音频流写入到声卡中
  data.sink = gst_element_factory_make ("autoaudiosink", "sink");
  data.videosink = gst_element_factory_make("autovideosink", "videosink");

  /* Create the empty pipeline */
  data.pipeline = gst_pipeline_new ("test-pipeline");

  if (!data.pipeline || !data.source || !data.convert || !data.resample
      || !data.sink || !data.videosink) {
    g_printerr ("Not all elements could be created.\n");
    return -1;
  }

  /* Build the pipeline. Note that we are NOT linking the source at this
   * point. We will do it later. */
  gst_bin_add_many (GST_BIN (data.pipeline), data.source, data.convert,
      data.resample, data.sink, data.videosink, NULL);
  // 将convert、resample和sink链接起来，但没有链接source，因为此时source还没有用于链接的source pads
  if (!gst_element_link_many (data.convert, data.resample, data.sink, NULL)) {
    g_printerr ("Elements could not be linked.\n");
    gst_object_unref (data.pipeline);
    return -1;
  }

  /* Set the URI to play */
  g_object_set (data.source, "uri",
      "file:///home/ts/Videos/Wonderland.mp4",
      NULL);

  /* Connect to the pad-added signal */
  // 通过回调的方式通知pad-added发生
  // 第一个参数是对象，第二个参数是对象的信号，第三个参数是回调，第四个是给回调函数的数据
  // GStreamer对这个数据指针不做任何操作，它只是将它转发给回调，这样我们就可以与它共享信息。
  g_signal_connect (data.source, "pad-added", G_CALLBACK (pad_added_handler),
      &data);

  /* Start playing */
  ret = gst_element_set_state (data.pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (data.pipeline);
    return -1;
  }

  /* Listen to the bus */
  bus = gst_element_get_bus (data.pipeline);
  do {
    msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
        GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

    /* Parse message */
    if (msg != NULL) {
      GError *err;
      gchar *debug_info;

      switch (GST_MESSAGE_TYPE (msg)) {
        case GST_MESSAGE_ERROR:
          gst_message_parse_error (msg, &err, &debug_info);
          g_printerr ("Error received from element %s: %s\n",
              GST_OBJECT_NAME (msg->src), err->message);
          g_printerr ("Debugging information: %s\n",
              debug_info ? debug_info : "none");
          g_clear_error (&err);
          g_free (debug_info);
          terminate = TRUE;
          break;
        case GST_MESSAGE_EOS:
          g_print ("End-Of-Stream reached.\n");
          terminate = TRUE;
          break;
        case GST_MESSAGE_STATE_CHANGED:
          /* We are only interested in state-changed messages from the pipeline */
          if (GST_MESSAGE_SRC (msg) == GST_OBJECT (data.pipeline)) {
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed (msg, &old_state, &new_state,
                &pending_state);
            g_print ("Pipeline state changed from %s to %s:\n",
                gst_element_state_get_name (old_state),
                gst_element_state_get_name (new_state));
          }
          break;
        default:
          /* We should not reach here */
          g_printerr ("Unexpected message received.\n");
          break;
      }
      gst_message_unref (msg);
    }
  } while (!terminate);

  /* Free resources */
  gst_object_unref (bus);
  gst_element_set_state (data.pipeline, GST_STATE_NULL);
  gst_object_unref (data.pipeline);
  return 0;
}

/* This function will be called by the pad-added signal */
// 当source element最终拥有足够的信息来开始生成数据时，它将创建source pad，并触发“pad-added”信号。此时，我们的回调将被调用
// src是触发信号的GstElement。new_pad是刚刚添加到src元素的GstPad。这通常是我们想要链接到的pad。date是我们附加到信号时提供的指针
static void
pad_added_handler (GstElement * src, GstPad * new_pad, CustomData * data)
{
  // 从CustomData中提取convert element，然后使用gst_element_get_static_pad检索其sink pad。得到了这个sink pad就可以和新创建的new_pad链接
  GstPad *sink_pad = gst_element_get_static_pad (data->convert, "sink");
  GstPad *video_sink_pad = gst_element_get_static_pad(data->videosink, "sink");
  GstPadLinkReturn ret;
  GstCaps *new_pad_caps = NULL;
  GstStructure *new_pad_struct = NULL;
  const gchar *new_pad_type = NULL;

  g_print ("Received new pad '%s' from '%s':\n", GST_PAD_NAME (new_pad),
      GST_ELEMENT_NAME (src));

  /* If our converter is already linked, we have nothing to do here */
  // 检查convert的sink pad是否被链接，这么做是因为uridecodebin可以创建它认为合适的任意数量的pad，对于每个pad，都会调用这个回调。
  // 一旦下游convert的sink pad已经链接了，就不去链接上游uridecodebin创建的新的source pad
  if (gst_pad_is_linked (sink_pad) && gst_pad_is_linked(video_sink_pad)) {
    g_print ("We are already linked both audio and video. Ignoring.\n");
    goto exit;
  }

  /* Check the new pad's type */
  // 如何确定新创建的source pad是不是下游sink pad想要链接的那个？目前我们关心的是音频数据
  // 检查这个新pad将要输出的数据类型
  // gst_pad_get_current_caps()检索pad的当前功能(即当前输出的数据类型)，并封装在GstCaps结构中。
  // 可以使用gst_pad_query_caps()查询pad都支持什么。
  new_pad_caps = gst_pad_get_current_caps (new_pad);
  // pad可以提供许多功能，因此GstCaps可以包含许多GstStructure，每个GstStructure代表不同的功能。
  new_pad_struct = gst_caps_get_structure (new_pad_caps, 0);
  new_pad_type = gst_structure_get_name (new_pad_struct);
  if (!g_str_has_prefix (new_pad_type, "audio/x-raw") && !g_str_has_prefix (new_pad_type, "video/x-raw")) {
    g_print ("It has type '%s' which is neither raw video nor raw video. Ignoring.\n",
        new_pad_type);
    goto exit;
  }

  /* Attempt the link */
  if (g_str_has_prefix (new_pad_type, "audio/x-raw")) {
      ret = gst_pad_link (new_pad, sink_pad);
  } else {
      ret = gst_pad_link (new_pad, video_sink_pad);
  }
  if (GST_PAD_LINK_FAILED (ret)) {
    g_print ("Type is '%s' but link failed.\n", new_pad_type);
  } else {
    g_print ("Link succeeded (type '%s').\n", new_pad_type);
  }

exit:
  /* Unreference the new pad's caps, if we got them */
  if (new_pad_caps != NULL)
    gst_caps_unref (new_pad_caps);

  /* Unreference the sink pad */
  gst_object_unref (sink_pad);
  gst_object_unref(video_sink_pad);
}
