/*
 * GStreamer gstreamer-ssdobjectdetector
 * Copyright (C) 2021 Collabora Ltd.
 *
 * gstssdobjectdetector.cpp
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-ssdobjectdetector
 * @short_description: Detect objects in video buffers using SSD neural network
 *
 * This element can parse per-buffer inference tensor meta data generated by an upstream
 * inference element
 *
 *
 * ## Example launch command:
 *
 * Test image file, model file (SSD) and label file can be found here :
 * https://gitlab.collabora.com/gstreamer/onnx-models
 *
 * GST_DEBUG=ssdobjectdetector:5 \
 * gst-launch-1.0 multifilesrc location=onnx-models/images/bus.jpg ! \
 * jpegdec ! videoconvert ! onnxinference execution-provider=cpu model-file=onnx-models/models/ssd_mobilenet_v1_coco.onnx !  \
 * ssdobjectdetector label-file=onnx-models/labels/COCO_classes.txt  ! videoconvert ! autovideosink
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstssdobjectdetector.h"

#include <gio/gio.h>

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/analytics/analytics.h>
#include "tensor/gsttensormeta.h"

/* Object detection tensor id strings */
#define GST_MODEL_OBJECT_DETECTOR_BOXES "Gst.Model.ObjectDetector.Boxes"
#define GST_MODEL_OBJECT_DETECTOR_SCORES "Gst.Model.ObjectDetector.Scores"
#define GST_MODEL_OBJECT_DETECTOR_NUM_DETECTIONS "Gst.Model.ObjectDetector.NumDetections"
#define GST_MODEL_OBJECT_DETECTOR_CLASSES "Gst.Model.ObjectDetector.Classes"

GST_DEBUG_CATEGORY_STATIC (ssd_object_detector_debug);
#define GST_CAT_DEFAULT ssd_object_detector_debug
GST_ELEMENT_REGISTER_DEFINE (ssd_object_detector, "ssdobjectdetector",
    GST_RANK_PRIMARY, GST_TYPE_SSD_OBJECT_DETECTOR);

/* GstSsdObjectDetector properties */
enum
{
  PROP_0,
  PROP_LABEL_FILE,
  PROP_SCORE_THRESHOLD,
  PROP_SIZE_THRESHOLD
};

#define GST_SSD_OBJECT_DETECTOR_DEFAULT_SCORE_THRESHOLD       0.3f      /* 0 to 1 */
#define GST_SSD_OBJECT_DETECTOR_DEFAULT_SIZE_THRESHOLD       0.9f       /* 0 to 1 */

static GstStaticPadTemplate gst_ssd_object_detector_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw")
    );

static GstStaticPadTemplate gst_ssd_object_detector_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw")
    );

static void gst_ssd_object_detector_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_ssd_object_detector_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_ssd_object_detector_finalize (GObject * object);
static GstFlowReturn gst_ssd_object_detector_transform_ip (GstBaseTransform *
    trans, GstBuffer * buf);
static gboolean gst_ssd_object_detector_process (GstBaseTransform * trans,
    GstBuffer * buf);
static gboolean
gst_ssd_object_detector_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps);

G_DEFINE_TYPE (GstSsdObjectDetector, gst_ssd_object_detector,
    GST_TYPE_BASE_TRANSFORM);

static void
gst_ssd_object_detector_class_init (GstSsdObjectDetectorClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *element_class = (GstElementClass *) klass;
  GstBaseTransformClass *basetransform_class = (GstBaseTransformClass *) klass;

  GST_DEBUG_CATEGORY_INIT (ssd_object_detector_debug, "ssdobjectdetector",
      0, "ssdobjectdetector");
  gobject_class->set_property = gst_ssd_object_detector_set_property;
  gobject_class->get_property = gst_ssd_object_detector_get_property;
  gobject_class->finalize = gst_ssd_object_detector_finalize;

  /**
   * GstSsdObjectDetector:label-file
   *
   * Label file
   *
   * Since: 1.24
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_LABEL_FILE,
      g_param_spec_string ("label-file",
          "Label file", "Label file", NULL, (GParamFlags)
          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  /**
   * GstSsdObjectDetector:score-threshold
   *
   * Threshold for deciding when to remove boxes based on score
   *
   * Since: 1.24
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SCORE_THRESHOLD,
      g_param_spec_float ("score-threshold",
          "Score threshold",
          "Threshold for deciding when to remove boxes based on score",
          0.0, 1.0, GST_SSD_OBJECT_DETECTOR_DEFAULT_SCORE_THRESHOLD,
          (GParamFlags)
          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  /**
   * GstSsdObjectDetector:size-threshold
   *
   * Threshold for deciding when to remove boxes based on proportion of the image
   *
   * Since: 1.26
   */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_SIZE_THRESHOLD,
      g_param_spec_float ("size-threshold",
          "Size threshold",
          "Threshold for deciding when to remove boxes based on proportion of the image",
          0.0, 1.0, GST_SSD_OBJECT_DETECTOR_DEFAULT_SIZE_THRESHOLD,
          (GParamFlags)
          (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata (element_class, "objectdetector",
      "Filter/Effect/Video",
      "Apply tensor output from inference to detect objects in video frames",
      "Aaron Boxer <aaron.boxer@collabora.com>, Marcus Edel <marcus.edel@collabora.com>");
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_ssd_object_detector_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_ssd_object_detector_src_template));
  basetransform_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_ssd_object_detector_transform_ip);
  basetransform_class->set_caps =
      GST_DEBUG_FUNCPTR (gst_ssd_object_detector_set_caps);
}

static void
gst_ssd_object_detector_init (GstSsdObjectDetector * self)
{
  self->size_threshold = GST_SSD_OBJECT_DETECTOR_DEFAULT_SIZE_THRESHOLD;
  self->score_threshold = GST_SSD_OBJECT_DETECTOR_DEFAULT_SCORE_THRESHOLD;
}

static void
gst_ssd_object_detector_finalize (GObject * object)
{
  GstSsdObjectDetector *self = GST_SSD_OBJECT_DETECTOR (object);

  g_free (self->label_file);
  g_clear_pointer (&self->labels, g_array_unref);

  G_OBJECT_CLASS (gst_ssd_object_detector_parent_class)->finalize (object);
}

static GArray *
read_labels (const char *labels_file)
{
  GArray *array;
  GFile *file = g_file_new_for_path (labels_file);
  GFileInputStream *file_stream;
  GDataInputStream *data_stream;
  GError *error = NULL;
  gchar *line;

  file_stream = g_file_read (file, NULL, &error);
  g_object_unref (file);
  if (!file_stream) {
    GST_WARNING ("Could not open file %s: %s\n", labels_file, error->message);
    g_clear_error (&error);
    return NULL;
  }

  data_stream = g_data_input_stream_new (G_INPUT_STREAM (file_stream));
  g_object_unref (file_stream);

  array = g_array_new (FALSE, FALSE, sizeof (GQuark));

  while ((line = g_data_input_stream_read_line (data_stream, NULL, NULL,
              &error))) {
    GQuark label = g_quark_from_string (line);
    g_array_append_val (array, label);
    g_free (line);
  }

  g_object_unref (data_stream);

  if (error) {
    GST_WARNING ("Could not open file %s: %s", labels_file, error->message);
    g_array_free (array, TRUE);
    g_clear_error (&error);
    return NULL;
  }

  if (array->len == 0) {
    g_array_free (array, TRUE);
    return NULL;
  }

  return array;
}

static void
gst_ssd_object_detector_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSsdObjectDetector *self = GST_SSD_OBJECT_DETECTOR (object);
  const gchar *filename;

  switch (prop_id) {
    case PROP_LABEL_FILE:
    {
      GArray *labels;

      filename = g_value_get_string (value);
      labels = read_labels (filename);

      if (labels) {
        g_free (self->label_file);
        self->label_file = g_strdup (filename);
        g_clear_pointer (&self->labels, g_array_unref);
        self->labels = labels;
      } else {
        GST_WARNING_OBJECT (self, "Label file '%s' not found!", filename);
      }
    }
      break;
    case PROP_SCORE_THRESHOLD:
      GST_OBJECT_LOCK (self);
      self->score_threshold = g_value_get_float (value);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_SIZE_THRESHOLD:
      GST_OBJECT_LOCK (self);
      self->size_threshold = g_value_get_float (value);
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ssd_object_detector_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSsdObjectDetector *self = GST_SSD_OBJECT_DETECTOR (object);

  switch (prop_id) {
    case PROP_LABEL_FILE:
      g_value_set_string (value, self->label_file);
      break;
    case PROP_SCORE_THRESHOLD:
      GST_OBJECT_LOCK (self);
      g_value_set_float (value, self->score_threshold);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_SIZE_THRESHOLD:
      GST_OBJECT_LOCK (self);
      g_value_set_float (value, self->size_threshold);
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstTensorMeta *
gst_ssd_object_detector_get_tensor_meta (GstSsdObjectDetector * object_detector,
    GstBuffer * buf)
{
  GstMeta *meta = NULL;
  gpointer iter_state = NULL;

  if (!gst_buffer_get_meta (buf, GST_TENSOR_META_API_TYPE)) {
    GST_DEBUG_OBJECT (object_detector,
        "missing tensor meta from buffer %" GST_PTR_FORMAT, buf);
    return NULL;
  }

  // find object detector meta

  while ((meta = gst_buffer_iterate_meta_filtered (buf, &iter_state,
              GST_TENSOR_META_API_TYPE))) {
    GstTensorMeta *tensor_meta = (GstTensorMeta *) meta;
    /* SSD model must have either 3 or 4 output tensor nodes: 4 if there is a label node,
     * and only 3 if there is no label  */
    if (tensor_meta->num_tensors != 3 && tensor_meta->num_tensors != 4)
      continue;

    gint boxesIndex = gst_tensor_meta_get_index_from_id (tensor_meta,
        g_quark_from_static_string (GST_MODEL_OBJECT_DETECTOR_BOXES));
    gint scoresIndex = gst_tensor_meta_get_index_from_id (tensor_meta,
        g_quark_from_static_string (GST_MODEL_OBJECT_DETECTOR_SCORES));
    gint numDetectionsIndex = gst_tensor_meta_get_index_from_id (tensor_meta,
        g_quark_from_static_string (GST_MODEL_OBJECT_DETECTOR_NUM_DETECTIONS));
    gint clasesIndex = gst_tensor_meta_get_index_from_id (tensor_meta,
        g_quark_from_static_string (GST_MODEL_OBJECT_DETECTOR_CLASSES));

    if (boxesIndex == GST_TENSOR_MISSING_ID
        || scoresIndex == GST_TENSOR_MISSING_ID
        || numDetectionsIndex == GST_TENSOR_MISSING_ID)
      continue;

    if (tensor_meta->num_tensors == 4 && clasesIndex == GST_TENSOR_MISSING_ID)
      continue;

    return tensor_meta;
  }

  return NULL;
}

static gboolean
gst_ssd_object_detector_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstSsdObjectDetector *self = GST_SSD_OBJECT_DETECTOR (trans);

  if (!gst_video_info_from_caps (&self->video_info, incaps)) {
    GST_ERROR_OBJECT (self, "Failed to parse caps");
    return FALSE;
  }

  return TRUE;
}

static GstFlowReturn
gst_ssd_object_detector_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  if (!gst_base_transform_is_passthrough (trans)) {
    if (!gst_ssd_object_detector_process (trans, buf)) {
      GST_ELEMENT_ERROR (trans, STREAM, FAILED,
          (NULL), ("ssd object detection failed"));
      return GST_FLOW_ERROR;
    }
  }

  return GST_FLOW_OK;
}

#define DEFINE_GET_FUNC(TYPE, MAX)                                      \
  static gboolean							\
  get_ ## TYPE ## _at_index (GstTensor *tensor, GstMapInfo *map,        \
      guint index, TYPE * out)                                          \
  {									\
    switch (tensor->data_type) {                                        \
  case GST_TENSOR_TYPE_FLOAT32: {					\
    float *f = (float *) map->data;                                     \
    if (sizeof(*f) * (index + 1) > map->size)				\
      return FALSE;							\
    *out = f[index];							\
    break;                                                              \
  }									\
  case GST_TENSOR_TYPE_UINT32: {					\
    guint32 *u = (guint32 *) map->data;                                 \
    if (sizeof(*u) * (index + 1) > map->size)				\
      return FALSE;							\
    *out = u[index];							\
    break;                                                              \
  }									\
  default:								\
    GST_ERROR ("Only float32 and int32 tensors are understood");	\
    return FALSE;							\
  }									\
  return TRUE;								\
  }

DEFINE_GET_FUNC (guint32, UINT32_MAX)
    DEFINE_GET_FUNC (float, FLOAT_MAX)
#undef DEFINE_GET_FUNC
     static void
         extract_bounding_boxes (GstSsdObjectDetector * self, gsize w, gsize h,
    GstAnalyticsRelationMeta * rmeta, GstTensorMeta * tmeta)
{
  gint classes_index;
  gint boxes_index;
  gint scores_index;
  gint numdetect_index;

  GstMapInfo boxes_map = GST_MAP_INFO_INIT;
  GstMapInfo numdetect_map = GST_MAP_INFO_INIT;
  GstMapInfo scores_map = GST_MAP_INFO_INIT;
  GstMapInfo classes_map = GST_MAP_INFO_INIT;

  guint num_detections = 0;

  classes_index = gst_tensor_meta_get_index_from_id (tmeta,
      g_quark_from_static_string (GST_MODEL_OBJECT_DETECTOR_CLASSES));
  numdetect_index = gst_tensor_meta_get_index_from_id (tmeta,
      g_quark_from_static_string (GST_MODEL_OBJECT_DETECTOR_NUM_DETECTIONS));
  scores_index = gst_tensor_meta_get_index_from_id (tmeta,
      g_quark_from_static_string (GST_MODEL_OBJECT_DETECTOR_SCORES));
  boxes_index = gst_tensor_meta_get_index_from_id (tmeta,
      g_quark_from_static_string (GST_MODEL_OBJECT_DETECTOR_BOXES));

  if (numdetect_index == GST_TENSOR_MISSING_ID
      || scores_index == GST_TENSOR_MISSING_ID
      || numdetect_index == GST_TENSOR_MISSING_ID) {
    GST_WARNING ("Missing tensor data expected for SSD model");
    return;
  }

  if (!gst_buffer_map (tmeta->tensor[numdetect_index].data, &numdetect_map,
          GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "Failed to map tensor memory for index %d",
        numdetect_index);
    goto cleanup;
  }

  if (!gst_buffer_map (tmeta->tensor[boxes_index].data, &boxes_map,
          GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "Failed to map tensor memory for index %d",
        boxes_index);
    goto cleanup;
  }

  if (!gst_buffer_map (tmeta->tensor[scores_index].data, &scores_map,
          GST_MAP_READ)) {
    GST_ERROR_OBJECT (self, "Failed to map tensor memory for index %d",
        scores_index);
    goto cleanup;
  }

  if (classes_index != GST_TENSOR_MISSING_ID &&
      !gst_buffer_map (tmeta->tensor[classes_index].data, &classes_map,
          GST_MAP_READ)) {
    GST_DEBUG_OBJECT (self, "Failed to map tensor memory for index %d",
        classes_index);
  }


  if (!get_guint32_at_index (&tmeta->tensor[numdetect_index], &numdetect_map,
          0, &num_detections)) {
    GST_ERROR_OBJECT (self, "Failed to get the number of detections");
    goto cleanup;
  }


  GST_LOG_OBJECT (self, "Model claims %d detections", num_detections);

  for (int i = 0; i < num_detections; i++) {
    float score;
    float x, y, bwidth, bheight;
    gint x_i, y_i, bwidth_i, bheight_i;
    guint32 bclass;
    GQuark label = 0;
    GstAnalyticsODMtd odmtd;

    if (!get_float_at_index (&tmeta->tensor[numdetect_index], &scores_map,
            i, &score))
      continue;

    GST_LOG_OBJECT (self, "Detection %u score is %f", i, score);
    if (score < self->score_threshold)
      continue;

    if (!get_float_at_index (&tmeta->tensor[boxes_index], &boxes_map,
            i * 4, &y))
      continue;
    if (!get_float_at_index (&tmeta->tensor[boxes_index], &boxes_map,
            i * 4 + 1, &x))
      continue;
    if (!get_float_at_index (&tmeta->tensor[boxes_index], &boxes_map,
            i * 4 + 2, &bheight))
      continue;
    if (!get_float_at_index (&tmeta->tensor[boxes_index], &boxes_map,
            i * 4 + 3, &bwidth))
      continue;

    if (CLAMP (bwidth, 0, 1) * CLAMP (bheight, 0, 1) > self->size_threshold) {
      GST_LOG_OBJECT (self, "Object at (%fx%f)=%f > %f, skipping",
          CLAMP (bwidth, 0, 1), CLAMP (bheight, 0, 1),
          CLAMP (bwidth, 0, 1) * CLAMP (bheight, 0, 1), self->size_threshold);
      continue;
    }

    if (self->labels && classes_map.memory &&
        get_guint32_at_index (&tmeta->tensor[classes_index], &classes_map,
            i, &bclass)) {
      if (bclass < self->labels->len)
        label = g_array_index (self->labels, GQuark, bclass);
    }

    x_i = x * w;
    y_i = y * h;
    bheight_i = (bheight * h) - y_i;
    bwidth_i = (bwidth * w) - x_i;

    if (gst_analytics_relation_meta_add_od_mtd (rmeta, label,
            x_i, y_i, bwidth_i, bheight_i, score, &odmtd))
      GST_DEBUG_OBJECT (self,
          "Object detected with label : %s, score: %f, bound box: %dx%d at (%d,%d)",
          g_quark_to_string (label), score, bwidth_i, bheight_i, x_i, y_i);
    else
      GST_WARNING_OBJECT (self, "Could not add detection to meta");
  }

cleanup:

  if (numdetect_map.memory)
    gst_buffer_unmap (tmeta->tensor[numdetect_index].data, &numdetect_map);
  if (classes_map.memory)
    gst_buffer_unmap (tmeta->tensor[classes_index].data, &classes_map);
  if (scores_map.memory)
    gst_buffer_unmap (tmeta->tensor[scores_index].data, &scores_map);
  if (boxes_map.memory)
    gst_buffer_unmap (tmeta->tensor[boxes_index].data, &boxes_map);
}


static gboolean
gst_ssd_object_detector_process (GstBaseTransform * trans, GstBuffer * buf)
{
  GstSsdObjectDetector *self = GST_SSD_OBJECT_DETECTOR (trans);
  GstTensorMeta *tmeta;
  GstAnalyticsRelationMeta *rmeta;

  // get all tensor metas
  tmeta = gst_ssd_object_detector_get_tensor_meta (self, buf);
  if (!tmeta) {
    GST_WARNING_OBJECT (trans, "missing tensor meta");
    return TRUE;
  } else {
    rmeta = gst_buffer_add_analytics_relation_meta (buf);
    g_assert (rmeta);
  }

  extract_bounding_boxes (self, self->video_info.width,
      self->video_info.height, rmeta, tmeta);

  return TRUE;
}
