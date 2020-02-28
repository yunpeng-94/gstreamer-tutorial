#include <gst/gst.h>

int
main (int argc, char *argv[])
{
  GstElement *pipeline, *source, *sink, *filter, *convert;
  GstBus *bus;
  GstMessage *msg;
  GstStateChangeReturn ret;

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  /* Create the elements */
  // 创建elements，第一个参数表示element的type，第二个参数表示element的name（在没有指针指向该element时方便检索，且可用于debug info）
  // 如果不指定name则Gstreamer会提供一个唯一的name
  source = gst_element_factory_make ("videotestsrc", "source");  // 创建一个测试的video source element
  sink = gst_element_factory_make ("autovideosink", "sink");  // autovideosink会根据平台自动选择并实例化最好的一个
  filter = gst_element_factory_make ("vertigotv", "filter");
  convert = gst_element_factory_make("videoconvert", "convert");

  /* Create the empty pipeline */
  // 创建一个pipeline并命名为test-pipeline
  pipeline = gst_pipeline_new ("test-pipeline");

  if (!pipeline || !source || !filter || !convert || !sink) {
    g_printerr ("Not all elements could be created.\n");
    return -1;
  }

  /* Build the pipeline */
  // pipeline是一种特殊的bin，第一个参数表示bin容器，如果容器是pipeline需要进行类型转换
  // 第一个参数后接要添加到pipeline中的elements，以NUll结尾
  // 可以使用gst_bin_add()添加单个元素
  gst_bin_add_many (GST_BIN (pipeline), source, filter, convert, sink, NULL);
  // 将elements关联起来，第一个参数是source element，第二个参数是destination element
  // 顺序很重要，因为必须根据数据流建立连接，且只有在同一个容器中的元素才能建立连接
//  if (gst_element_link (source, sink) != TRUE) {
//    g_printerr ("Elements could not be linked.\n");
//    gst_object_unref (pipeline);
//    return -1;
//  }
  gst_element_link_many(source, filter, convert, sink, NULL);

  /* Modify the source's properties */
  // 修改element属性，可同时修改多个属性，接受一个以NULL结尾的属性名、属性值对列表
  // pattern属性控制元素输出的测试视频的类型
  // 可以使用gst-inspect-1.0工具找到element公开的所有属性的名称和可能的值
  g_object_set (source, "pattern", 0, NULL);

  /* Start playing */
  // 改为播放状态
  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (pipeline);
    return -1;
  }

  /* Wait until error or EOS */
  // 等待执行结束
  bus = gst_element_get_bus (pipeline);
  // 在遇到ERROR或EOS的时候要求函数返回
  msg =
      gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE,
      GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

  /* Parse message */
  if (msg != NULL) {
    GError *err;
    gchar *debug_info;

    // 使用GST_MESSAGE_TYPE宏查看错误类型
    switch (GST_MESSAGE_TYPE (msg)) {
      case GST_MESSAGE_ERROR:
        // 解析错误，获取对调试有用的信息
        gst_message_parse_error (msg, &err, &debug_info);
        g_printerr ("Error received from element %s: %s\n",
            GST_OBJECT_NAME (msg->src), err->message);
        g_printerr ("Debugging information: %s\n",
            debug_info ? debug_info : "none");
        g_clear_error (&err);
        g_free (debug_info);
        break;
      case GST_MESSAGE_EOS:
        g_print ("End-Of-Stream reached.\n");
        break;
      default:
        /* We should not reach here because we only asked for ERRORs and EOS */
        g_printerr ("Unexpected message received.\n");
        break;
    }
    gst_message_unref (msg);
  }

  /* Free resources */
  gst_object_unref (bus);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  return 0;
}
