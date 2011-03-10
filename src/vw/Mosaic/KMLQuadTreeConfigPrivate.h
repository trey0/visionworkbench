// __BEGIN_LICENSE__
// Copyright (C) 2006-2010 United States Government as represented by
// the Administrator of the National Aeronautics and Space Administration.
// All Rights Reserved.
// __END_LICENSE__


#include <vw/Mosaic/KMLQuadTreeConfig.h>

#include <vw/Image.h>

#include <iomanip>
#include <sstream>
#include <boost/bind.hpp>

#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/convenience.hpp>
namespace fs = boost::filesystem;

using vw::cartography::GeoReference;

namespace vw {

namespace mosaic {

  struct KMLQuadTreeConfigData {
    BBox2 m_longlat_bbox;
    std::string m_title;
    int m_max_lod_pixels;
    int m_draw_order_offset;
    std::string m_metadata;
    mutable std::ostringstream m_root_node_tags;

    std::string kml_latlonbox( BBox2 const& longlat_bbox, bool alt ) const;
    std::string kml_network_link( std::string const& name, std::string const& href, BBox2 const& longlat_bbox, int min_lod_pixels ) const;
    std::string kml_ground_overlay( std::string const& href, BBox2 const& region_bbox, BBox2 const& image_bbox, int draw_order, int min_lod_pixels, int max_lod_pixels ) const;
    BBox2 pixels_to_longlat( BBox2i const& image_bbox, Vector2i const& dimensions ) const;

    void metadata_func( QuadTreeGenerator const&, QuadTreeGenerator::TileInfo const& info ) const;
    boost::shared_ptr<DstImageResource> tile_resource_func( QuadTreeGenerator const&, QuadTreeGenerator::TileInfo const& info, ImageFormat const& format ) const;

  public:
    KMLQuadTreeConfigData()
      : m_longlat_bbox(-180,-90,360,180),
        m_max_lod_pixels(1024),
        m_draw_order_offset(0)
    {}
  };

} // namespace mosaic
} // namespace vw
