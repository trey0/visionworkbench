// __BEGIN_LICENSE__
// Copyright (C) 2006-2010 United States Government as represented by
// the Administrator of the National Aeronautics and Space Administration.
// All Rights Reserved.
// __END_LICENSE__


/// \file Detector.h
///
/// Built-in classes and functions for performing interest point detection.
///
#ifndef __VW_INTERESTPOINT_DETECTOR_H__
#define __VW_INTERESTPOINT_DETECTOR_H__

#include <vw/Core/Debugging.h>
#include <vw/Core/Settings.h>
#include <vw/Core/ThreadPool.h>
#include <vw/Image/ImageViewRef.h>
#include <vw/Image/Statistics.h>
#include <vw/Image/Filter.h>

#include <vw/InterestPoint/InterestData.h>
#include <vw/InterestPoint/Extrema.h>
#include <vw/InterestPoint/Localize.h>
#include <vw/InterestPoint/InterestOperator.h>
#include <vw/InterestPoint/WeightedHistogram.h>
#include <vw/InterestPoint/ImageOctaveHistory.h>

namespace vw {
namespace ip {

  /// A CRTP InterestDetector base class.
  ///
  /// Subclasses of InterestDetectorBase must provide an
  /// implementation that detects interest points in an image region.
  /// The call operator() above calls this method, passing in each
  /// individual patch to be processed. That declaration is expected
  /// to be of the form:
  ///
  ///    template <class ViewT>
  ///    InterestPointList process_image(ImageViewBase<ViewT> const& image) const {
  ///
  template <class ImplT>
  struct InterestDetectorBase {

    /// \cond INTERNAL
    // Methods to access the derived type
    inline ImplT& impl() { return static_cast<ImplT&>(*this); }
    inline ImplT const& impl() const { return static_cast<ImplT const&>(*this); }
    /// \endcond

    /// Find the interest points in an image using the provided detector.
    ///
    /// Some images are too large to be processed for interest points all
    /// at once.  If the user specifies a max_interestpoint_image_dimension,
    /// this value is used to segment the image into smaller images which
    /// are passed individually to the interest point detector.  This routine
    /// combines the interest points from the sub-images once detection is
    /// complete.  Be aware that a few interest points along the segment
    /// borders may be lost.  A good max dimension depends on the amount
    /// of RAM needed by the detector (and the total RAM available).  A
    /// value of 2048 seems to work well in most cases.
    template <class ViewT>
    InterestPointList operator() (vw::ImageViewBase<ViewT> const& image,
                                  const int32 /*max_interestpoint_image_dimension*/ = 0) {

      InterestPointList interest_points;
      vw_out(DebugMessage, "interest_point") << "Finding interest points in block: [ " << image.impl().cols() << " x " << image.impl().rows() << " ]\n";

      // Note: We explicitly convert the image to PixelGray<float>
      // (rescaling as necessary) here before passing it off to the
      // rest of the interest detector code.
      interest_points = impl().process_image(pixel_cast<PixelGray<float> >(channel_cast_rescale<float>(image.impl())));

      vw_out(DebugMessage, "interest_point") << "Finished processing block. Found " << interest_points.size() << " interest points.\n";
      return interest_points;
    }

  };


  template <class ViewT, class DetectorT>
  class InterestPointDetectionTask : public Task, private boost::noncopyable {

    ViewT m_view;
    DetectorT& m_detector;
    BBox2i m_bbox;
    InterestPointList& m_interest_point_list;
    Mutex& m_mutex;
    int m_id, m_max_id;

  public:
    InterestPointDetectionTask(ViewT const& view, DetectorT& detector, BBox2i bbox,
                               InterestPointList& ip_list, Mutex &mutex, int id, int max_id ) :
      m_view(view), m_detector(detector), m_bbox(bbox),
      m_interest_point_list(ip_list), m_mutex(mutex), m_id(id), m_max_id(max_id) {}

    void operator()() {
      vw_out(InfoMessage, "interest_point") << "Locating interest points in block " << m_id << "/" << m_max_id << "   [ " << m_bbox << " ]\n";
      InterestPointList new_ip_list = m_detector(crop(pixel_cast<PixelGray<float> >(channel_cast_rescale<float>(m_view.impl())), m_bbox),0);
      for (InterestPointList::iterator pt = new_ip_list.begin(); pt != new_ip_list.end(); ++pt) {
        (*pt).x +=  m_bbox.min().x();
        (*pt).ix += m_bbox.min().x();
        (*pt).y +=  m_bbox.min().y();
        (*pt).iy += m_bbox.min().y();
      }

      // Append these interest points to the master list owned by the
      // detect_interest_points() function.
      {
        Mutex::Lock lock(m_mutex);
        m_interest_point_list.splice(m_interest_point_list.end(), new_ip_list);
      }
    }
    InterestPointList interest_point_list() { return m_interest_point_list; }
  };

  /// This free function implements a multithreaded interest point
  /// detector.  Threads are spun off to process the image in
  /// 2048x2048 pixel blocks.
  template <class ViewT, class DetectorT>
  InterestPointList detect_interest_points (ViewT const& view, DetectorT& detector) {
    typedef InterestPointDetectionTask<ViewT, DetectorT> task_type;

    FifoWorkQueue queue(vw_settings().default_num_threads());
    InterestPointList ip_list;
    Mutex mutex;     // Used to lock access to ip_list by the child threads.

    vw_out(DebugMessage, "interest_point") << "Running MT interest point detector.  Input image: [ " << view.impl().cols() << " x " << view.impl().rows() << " ]\n";

    // Process the image in 1024x1024 pixel blocks.
    int tile_size = vw_settings().default_tile_size();
    if (tile_size < 1024) tile_size = 1024;
    std::vector<BBox2i> bboxes = image_blocks(view.impl(),
                                              tile_size, tile_size);
    for (unsigned i = 0; i < bboxes.size(); ++i) {
      boost::shared_ptr<task_type> task (new task_type(view, detector, bboxes[i], ip_list, mutex, i+1, bboxes.size() ) );
      queue.add_task(task);
    }
    vw_out(DebugMessage, "interest_point") << "Waiting for threads to terminate.\n";
    queue.join_all();

    vw_out(DebugMessage, "interest_point") << "MT interest point detection complete.  " << ip_list.size() << " interest point detected.\n";
    return ip_list;
  }

  /// Get the orientation of the point at (i0,j0,k0).  This is done by
  /// computing a gaussian weighted average orientations in a region
  /// around the detected point.  This is not the most sophisticated
  /// method for determing orientations -- for example, if there is
  /// more than one dominant edge orientation in an image, this will
  /// produce a blended average of those two directions, and if those
  /// two directions are opposites, then they will cancel each other
  /// out.  However, this seems to work well enough for the time
  /// being.
  template <class OriT, class MagT>
  float get_orientation( ImageViewBase<OriT> const& x_grad,
                         ImageViewBase<MagT> const& y_grad,
                         float i0, float j0, float sigma_ratio = 1.0) {

    // The size, in pixels, of the image patch used to compute the orientation.
    static const int IP_ORIENTATION_WIDTH = 10;

    // Nominal feature support patch is WxW at the base scale, with
    // W = IP_ORIENTATION_HALF_WIDTH * 2 + 1, and
    // we multiply by sigma[k]/sigma[1] for other planes.
    //
    // Get bounds for scaled WxW window centered at (i,j) in plane k
    int halfwidth = (int)(IP_ORIENTATION_WIDTH/2*sigma_ratio + 0.5);
    int left  = int(roundf(i0 - halfwidth));
    int top   = int(roundf(j0 - halfwidth));

    // Compute (gaussian weight)*(edge magnitude) kernel
    ImageView<float> weight(IP_ORIENTATION_WIDTH,IP_ORIENTATION_WIDTH);
    make_gaussian_kernel_2d( weight, 6 * sigma_ratio, IP_ORIENTATION_WIDTH );

    // We must compute the average orientation in quadrature.
    double weight_sum = sum_of_pixel_values(weight);

    // Compute the gaussian weighted average x_gradient
    ImageView<float> weighted_grad = weight * crop(edge_extend(x_grad.impl()),left,top,IP_ORIENTATION_WIDTH,IP_ORIENTATION_WIDTH);
    double avg_x_grad = sum_of_pixel_values(weighted_grad) / weight_sum;

    // Compute the gaussian weighted average y_gradient
    weighted_grad = weight * crop(edge_extend(y_grad.impl()),left,top,IP_ORIENTATION_WIDTH,IP_ORIENTATION_WIDTH);
    double avg_y_grad = sum_of_pixel_values(weighted_grad) / weight_sum;

    return atan2(avg_y_grad,avg_x_grad);
  }


  /// This class performs interest point detection on a source image
  /// without using scale space methods.
  template <class InterestT>
  class InterestPointDetector : public InterestDetectorBase<InterestPointDetector<InterestT> > {
  public:

    /// Setting max_points = 0 will disable interest point culling.
    /// Otherwies, the max_points most "interesting" points are
    /// returned.
    InterestPointDetector(int max_points = 1000) : m_interest(InterestT()), m_max_points(max_points) {}

    /// Setting max_points = 0 will disable interest point culling.
    /// Otherwies, the max_points most "interesting" points are
    /// returned.
    InterestPointDetector(InterestT const& interest, int max_points = 1000) : m_interest(interest), m_max_points(max_points) {}

    /// Detect interest points in the source image.
    template <class ViewT>
    InterestPointList process_image(ImageViewBase<ViewT> const& image) const {
      Timer total("\tTotal elapsed time", DebugMessage, "interest_point");

      // Calculate gradients, orientations and magnitudes
      vw_out(DebugMessage, "interest_point") << "\n\tCreating image data... ";
      Timer *timer = new Timer("done, elapsed time", DebugMessage, "interest_point");

      // We blur the image by a small amount to eliminate any noise
      // that might confuse the interest point detector.
      typedef SeparableConvolutionView<ViewT, typename DefaultKernelT<typename ViewT::pixel_type>::type, ConstantEdgeExtension> blurred_image_type;
      ImageInterestData<blurred_image_type, InterestT> img_data(gaussian_filter(image,0.5));

      delete timer;

      // Compute interest image
      vw_out(DebugMessage, "interest_point") << "\tComputing interest image... ";
      {
        Timer t("done, elapsed time", DebugMessage, "interest_point");
        m_interest(img_data);
      }

      // Find extrema in interest image
      vw_out(DebugMessage, "interest_point") << "\tFinding extrema... ";
      InterestPointList points;
      {
        Timer t("elapsed time", DebugMessage, "interest_point");
        find_extrema(points, img_data);
        vw_out(DebugMessage, "interest_point") << "done (" << points.size() << " interest points), ";
      }

      // Subpixel localization
      vw_out(DebugMessage, "interest_point") << "\tLocalizing... ";
      {
        Timer t("elapsed time", DebugMessage, "interest_point");
        localize(points, img_data);
        vw_out(DebugMessage, "interest_point") << "done (" << points.size() << " interest points), ";
      }

      // Threshold (after localization)
      vw_out(DebugMessage, "interest_point") << "\tThresholding... ";
      {
        Timer t("elapsed time", DebugMessage, "interest_point");
        threshold(points, img_data);
        vw_out(DebugMessage, "interest_point") << "done (" << points.size() << " interest points), ";
      }

      // Cull (limit the number of interest points to the N "most interesting" points)
      vw_out(DebugMessage, "interest_point") << "\tCulling Interest Points (limit is set to " << m_max_points << " points)... ";
      {
        Timer t("elapsed time", DebugMessage, "interest_point");
        int original_num_points = points.size();
        points.sort();
        if ((m_max_points > 0) && (m_max_points < original_num_points))
           points.resize(m_max_points);
        vw_out(DebugMessage, "interest_point") << "done (removed " << original_num_points - points.size() << " interest points, " << points.size() << " remaining.), ";
      }

      // Assign orientations
      vw_out(DebugMessage, "interest_point") << "\tAssigning orientations... ";
      {
        Timer t("elapsed time", DebugMessage, "interest_point");
        assign_orientations(points, img_data);
        vw_out(DebugMessage, "interest_point") << "done (" << points.size() << " interest points), ";
      }

      // Return vector of interest points
      return points;
    }

  protected:
    InterestT m_interest;
    int m_max_points;

    // By default, use find_peaks in Extrema.h
    template <class DataT>
    inline int find_extrema(InterestPointList& points, DataT const& img_data) const {
      return find_peaks(points, img_data);
    }

    // By default, use fit_peak in Localize.h
    template <class DataT>
    inline void localize(InterestPointList& points, DataT const& img_data) const {
      // TODO: Remove points rejected by localizer
      for (InterestPointList::iterator i = points.begin(); i != points.end(); ++i) {
        fit_peak(img_data.interest(), *i);
      }
    }

    template <class DataT>
    inline void threshold(InterestPointList& points, DataT const& img_data) const {
      // TODO: list::remove_if
      InterestPointList::iterator pos = points.begin();
      while (pos != points.end()) {
        if (!m_interest.threshold(*pos, img_data))
          pos = points.erase(pos);
        else
          pos++;
      }
    }

    template <class DataT>
    void assign_orientations(InterestPointList& points, DataT const& img_data) const {

//       // Save the X gradient
//       ImageView<float> grad_x_image = normalize(img_data.gradient_x());
//       vw::write_image("grad_x1.jpg", grad_x_image);

//       // Save the Y gradient
//       ImageView<float> grad_y_image = normalize(img_data.gradient_y());
//       vw::write_image("grad_y1.jpg", grad_y_image);

      for (InterestPointList::iterator i = points.begin(); i != points.end(); ++i) {
        i->orientation = get_orientation(img_data.gradient_x(), img_data.gradient_y(), i->x, i->y);
      }
    }

    /// This method dumps the various images internal to the detector out
    /// to files for visualization and debugging.  The images written out
    /// are the x and y gradients, edge orientation and magnitude, and
    /// interest function values for the source image.
    template <class DataT>
    void write_images(DataT const& img_data) const {
      // Save the X gradient
      ImageView<float> grad_x_image = normalize(img_data.gradient_x());
      vw::write_image("grad_x.jpg", grad_x_image);

      // Save the Y gradient
      ImageView<float> grad_y_image = normalize(img_data.gradient_y());
      vw::write_image("grad_y.jpg", grad_y_image);

      // Save the edge orientation image
      ImageView<float> ori_image = normalize(img_data.orientation());
      vw::write_image("ori.jpg", ori_image);

      // Save the edge magnitude image
      ImageView<float> mag_image = normalize(img_data.magnitude());
      vw::write_image("mag.jpg", mag_image);

      // Save the interest function image
      ImageView<float> interest_image = normalize(img_data.interest());
      vw::write_image("interest.jpg", interest_image);
    }
  };


  /// This class performs interest point detection on a source image
  /// making use of scale space methods to achieve scale invariance.
  /// This assumes that the detector works properly over different
  /// choices of scale.
  template <class InterestT>
  class ScaledInterestPointDetector : public InterestDetectorBase<ScaledInterestPointDetector<InterestT> >,
                                      private boost::noncopyable {

  public:
    // TODO: choose number of octaves based on image size
    static const int IP_DEFAULT_SCALES = 3;
    static const int IP_DEFAULT_OCTAVES = 3;

    /// Setting max_points = 0 will disable interest point culling.
    /// Otherwies, the max_points most "interesting" points are
    /// returned.
    ScaledInterestPointDetector(int max_points = 1000)
      : m_interest(InterestT()), m_scales(IP_DEFAULT_SCALES), m_octaves(IP_DEFAULT_OCTAVES), m_max_points(max_points) {}

    ScaledInterestPointDetector(InterestT const& interest, int max_points = 1000)
      : m_interest(interest), m_scales(IP_DEFAULT_SCALES), m_octaves(IP_DEFAULT_OCTAVES), m_max_points(max_points) {}

    ScaledInterestPointDetector(InterestT const& interest, int scales, int octaves, int max_points = 1000)
      : m_interest(interest), m_scales(scales), m_octaves(octaves), m_max_points(max_points) {}

    /// Detect interest points in the source image.
    template <class ViewT>
    InterestPointList process_image(ImageViewBase<ViewT> const& image) const {
      typedef ImageInterestData<ImageView<typename ViewT::pixel_type>,InterestT> DataT;

      Timer total("\t\tTotal elapsed time", DebugMessage, "interest_point");

      // Create scale space
      vw_out(DebugMessage, "interest_point") << "\n\tCreating initial image octave... ";
      Timer *t_oct = new Timer("done, elapsed time", DebugMessage, "interest_point");

      // We blur the image by a small amount to eliminate any noise
      // that might confuse the interest point detector.
      ImageOctave<typename DataT::source_type> octave(gaussian_filter(image,0.5), m_scales);

      delete t_oct;

      InterestPointList points;
      std::vector<DataT> img_data;

      for (int o = 0; o < m_octaves; ++o) {
        Timer t_loop("\t\tElapsed time for octave", DebugMessage, "interest_point");

        // Calculate intermediate data (gradients, orientations, magnitudes)
        vw_out(DebugMessage, "interest_point") << "\tCreating image data... ";
        {
          Timer t("done, elapsed time", DebugMessage, "interest_point");
          img_data.clear();
          img_data.reserve(octave.num_planes);
          for (int k = 0; k < octave.num_planes; ++k) {
            img_data.push_back(DataT(octave.scales[k]));
          }
        }

        // Compute interest images
        vw_out(DebugMessage, "interest_point") << "\tComputing interest images... ";
        {
          Timer t("done, elapsed time", DebugMessage, "interest_point");
          for (int k = 0; k < octave.num_planes; ++k) {
            m_interest(img_data[k], octave.plane_index_to_scale(k));
          }
        }

        // Find extrema in interest image
        vw_out(DebugMessage, "interest_point") << "\tFinding extrema... ";
        InterestPointList new_points;
        {
          Timer t("elapsed time", DebugMessage, "interest_point");
          find_extrema(new_points, img_data, octave);
          vw_out(DebugMessage, "interest_point") << "done (" << new_points.size() << " extrema found), ";
        }

        // Subpixel localization
        vw_out(DebugMessage, "interest_point") << "\tLocalizing... ";
        {
          Timer t("elapsed time", DebugMessage, "interest_point");
          localize(new_points, img_data, octave);
          vw_out(DebugMessage, "interest_point") << "done (" << new_points.size() << " interest points), ";
        }

        // Threshold
        vw_out(DebugMessage, "interest_point") << "\tThresholding... ";
        {
          Timer t("elapsed time", DebugMessage, "interest_point");
          threshold(new_points, img_data, octave);
          vw_out(DebugMessage, "interest_point") << "done (" << new_points.size() << " interest points), ";
        }

        // Cull (limit the number of interest points to the N "most interesting" points)
        vw_out(DebugMessage, "interest_point") << "\tCulling Interest Points (limit is set to " << m_max_points << " points)... ";
        {
          Timer t("elapsed time", DebugMessage, "interest_point");
          int original_num_points = new_points.size();
          new_points.sort();
          if ((m_max_points > 0) && (m_max_points/m_octaves < (int)new_points.size()))
            new_points.resize(m_max_points/m_octaves);
          vw_out(DebugMessage, "interest_point") << "done (removed " << original_num_points - new_points.size() << " interest points, " << new_points.size() << " remaining.), ";
        }

        // Assign orientations
        vw_out(DebugMessage, "interest_point") << "\tAssigning orientations... ";
        {
          Timer t("elapsed time", DebugMessage, "interest_point");
          assign_orientations(new_points, img_data, octave);
          vw_out(DebugMessage, "interest_point") << "done (" << new_points.size() << " interest points), ";
        }

        // Scale subpixel location to move back to original coords
        for (InterestPointList::iterator i = new_points.begin(); i != new_points.end(); ++i) {
          i->x *= octave.base_scale;
          i->y *= octave.base_scale;
          // TODO: make sure this doesn't screw up any post-processing
          i->ix = (int)( i->x + 0.5 );
          i->iy = (int)( i->y + 0.5 );
        }

        // Add newly found interest points
        points.splice(points.end(), new_points);

        // Build next octave of scale space
        if (o != m_octaves - 1) {
          vw_out(DebugMessage, "interest_point") << "\tBuilding next octave... ";
          Timer t("done, elapsed time", DebugMessage);
          octave.build_next();
        }
      }

      return points;
    }

  protected:
    InterestT m_interest;
    int m_scales, m_octaves, m_max_points;

    // By default, uses find_peaks in Extrema.h
    template <class DataT, class ViewT>
    inline int find_extrema(InterestPointList& points,
                            std::vector<DataT> const& img_data,
                            ImageOctave<ViewT> const& octave) const {
      return find_peaks(points, img_data, octave);
    }

    // By default, uses fit_peak in Localize.h
    template <class DataT, class ViewT>
    inline int localize(InterestPointList& points,
                        std::vector<DataT> const& img_data,
                        ImageOctave<ViewT> const& octave) const {
      for (InterestPointList::iterator i = points.begin(); i != points.end(); ++i) {
        fit_peak(img_data, *i, octave);
      }

      return 0;
    }

    template <class DataT, class ViewT>
    inline void threshold(InterestPointList& points,
                         std::vector<DataT> const& img_data,
                         ImageOctave<ViewT> const& octave) const {
      // TODO: list::remove_if
      InterestPointList::iterator pos = points.begin();
      while (pos != points.end()) {
        int k = octave.scale_to_plane_index(pos->scale);
        if (!m_interest.threshold(*pos, img_data[k]))
          pos = points.erase(pos);
        else
          pos++;
      }
    }

    template <class DataT, class ViewT>
    void assign_orientations(InterestPointList& points,
                             std::vector<DataT> const& img_data,
                             ImageOctave<ViewT> const& octave) const {

      for (InterestPointList::iterator i = points.begin(); i != points.end(); ++i) {
        int k = octave.scale_to_plane_index(i->scale);
        i->orientation = get_orientation(img_data[k].gradient_x(), img_data[k].gradient_y(),
                                         i->x, i->y, octave.sigma[k]/octave.sigma[1]);
      }
    }

    /// This method dumps the various images internal to the detector out
    /// to files for visualization and debugging.  The images written out
    /// are the x and y gradients, edge orientation and magnitude, and
    /// interest function values for all planes in the octave processed.
    template <class DataT>
    int write_images(std::vector<DataT> const& img_data) const {
      for (int k = 0; k < img_data.size(); ++k){
        int imagenum = k;
        char fname[256];

        // Save the scale
        sprintf( fname, "scale_%02d.jpg", imagenum );
        ImageView<float> scale_image = normalize(img_data[k].source());
        vw::write_image(fname, scale_image);

        // Save the X gradient
        sprintf( fname, "grad_x_%02d.jpg", imagenum );
        ImageView<float> grad_x_image = normalize(img_data[k].gradient_x());
        vw::write_image(fname, grad_x_image);

        // Save the Y gradient
        sprintf( fname, "grad_y_%02d.jpg", imagenum );
        ImageView<float> grad_y_image = normalize(img_data[k].gradient_y());
        vw::write_image(fname, grad_y_image);

        // Save the edge orientation image
        sprintf( fname, "ori_%02d.jpg", imagenum );
        ImageView<float> ori_image = normalize(img_data[k].orientation());
        vw::write_image(fname, ori_image);

        // Save the edge magnitude image
        sprintf( fname, "mag_%02d.jpg", imagenum );
        ImageView<float> mag_image = normalize(img_data[k].magnitude());
        vw::write_image(fname, mag_image);

        // Save the interest function image
        sprintf( fname, "interest_%02d.jpg", imagenum );
        ImageView<float> interest_image = normalize(img_data[k].interest());
        vw::write_image(fname, interest_image);
      }
      return 0;
    }
  };

}} // namespace vw::ip

#endif // __VW_INTERESTPOINT_DETECTOR_H__
