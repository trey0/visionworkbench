// __BEGIN_LICENSE__
// Copyright (C) 2006-2010 United States Government as represented by
// the Administrator of the National Aeronautics and Space Administration.
// All Rights Reserved.
// __END_LICENSE__


/// \file KMLQuadTreeConfig.h
///
/// A configuration class that provides callbacks for
/// QuadTreeGenerator that generate multi-resolution
/// KML overlays.
///
#ifndef __VW_MOSAIC_KMLQUADTREECONFIG_H__
#define __VW_MOSAIC_KMLQUADTREECONFIG_H__

#include <vw/FileIO/DiskImageResourcePNG.h>

#include <vw/Mosaic/QuadTreeGenerator.h>
#include <vw/Mosaic/QuadTreeConfig.h>

namespace vw {

  // This wrapper class intercepts premultiplied alpha data being written
  // to a PNG resource, and implements a pyramid-based hole-filling
  // algorithm to extrapolate data into the alpha-masked regions of the
  // image.
  //
  // This is a workaround for a Google Earth bug, in which GE's
  // rendering of semi-transparent GroundOverlays interpolates
  // alpha-masked (i.e. invalid) data, resulting in annoying (generally
  // black) fringes around semi-transparent images.
  class DiskImageResourcePNGAlpha : public DiskImageResourcePNG {
  public:
      DiskImageResourcePNGAlpha( std::string const& filename, ImageFormat const& format );
      void write( ImageBuffer const& src, BBox2i const& bbox );
  };

namespace mosaic {

  struct KMLQuadTreeConfigData;

  cartography::GeoReference kml_output_georef(uint32 xresolution, uint32 yresolution = 0);

  class KMLQuadTreeConfig : public QuadTreeConfig {
  public:
    KMLQuadTreeConfig();
    virtual ~KMLQuadTreeConfig() {}

    // Set the extents (in degrees) of the quadtree.
    void set_longlat_bbox( BBox2 const& bbox );

    // Set the title of the root KML file.
    void set_title( std::string const& title );

    // Set the maximum level of detail (in pixels) at which each resolution
    // of the quadtree is displayed.
    void set_max_lod_pixels( int32 pixels );

    // Set the drawOrder offset.  Overlays with positive offets are drawn on top.
    void set_draw_order_offset( int32 offset );

    // Set an option string of additional metadata to be included in the root KML file.
    void set_metadata( std::string const& data );

    // Configure the given quadtree to generate this KML.  This enables image
    // cropping and sets the image path function, branch function, and metadata
    // function.  If you intend to override any of these, be sure to do so
    // *after* calling configure() or your changes will be overwritten.
    void configure( QuadTreeGenerator& qtree ) const;

    cartography::GeoReference output_georef(uint32 xresolution, uint32 yresolution = 0);

    std::vector<std::pair<std::string,vw::BBox2i> > branch_func( QuadTreeGenerator const&, std::string const& name, BBox2i const& region ) const;

  protected:
    // The implementation is stored in a shared pointer so that it can
    // be safely bound to the quadtree callbacks in colsures even if
    // this config object goes out of scope.
    boost::shared_ptr<KMLQuadTreeConfigData> m_data;
  };

} // namespace mosaic
} // namespace vw

#endif // __VW_MOSAIC_KMLQUADTREECONFIG_H__
