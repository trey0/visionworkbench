// __BEGIN_LICENSE__
// Copyright (C) 2006-2010 United States Government as represented by
// the Administrator of the National Aeronautics and Space Administration.
// All Rights Reserved.
// __END_LICENSE__


#include <vw/Mosaic/UTMKMLQuadTreeConfig.h>
#include <vw/Mosaic/KMLQuadTreeConfigPrivate.h>

#include <vw/Image.h>
#include <vw/FileIO/DiskImageResourcePNG.h>

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

  UTMKMLQuadTreeConfig::UTMKMLQuadTreeConfig() {}

  void UTMKMLQuadTreeConfig::configure( QuadTreeGenerator& qtree ) const {
    KMLQuadTreeConfig::configure(qtree);
    qtree.set_branch_func( boost::bind(&UTMKMLQuadTreeConfig::branch_func,this,_1,_2,_3) );
  }

  GeoReference UTMKMLQuadTreeConfig::get_output_georef() const {
    return get_output_georef(m_xresolution, m_yresolution);
  }

  GeoReference UTMKMLQuadTreeConfig::get_output_georef(uint32 xresolution, uint32 yresolution) const {
    GeoReference r;
    r.set_pixel_interpretation(GeoReference::PixelAsArea);
    r.set_UTM(abs(m_output_utm_zone), m_output_utm_zone > 0);

    // Note: the global UTMKML pixel space extends to +/- 180 degrees
    // latitude as well as longitude.
    Matrix3x3 transform;
    transform(0,0) = 360.0 / xresolution;
    transform(0,2) = -180;
    transform(1,1) = -360.0 / yresolution;
    transform(1,2) = 180;
    transform(2,2) = 1;
    r.set_transform(transform);

    return r;
  }

  GeoReference UTMKMLQuadTreeConfig::output_georef(uint32 xresolution, uint32 yresolution) {
    m_xresolution = xresolution;
    m_yresolution = yresolution;
    return get_output_georef(xresolution, yresolution);
  }

  std::vector<std::pair<std::string,vw::BBox2i> > UTMKMLQuadTreeConfig::branch_func( QuadTreeGenerator const& qtree, std::string const& name, BBox2i const& region ) const {
    std::vector<std::pair<std::string,vw::BBox2i> > children;
    if( region.height() > qtree.get_tile_size() ) {

      GeoReference out = get_output_georef();
      Vector2 southwest = out.lonlat_to_point(m_data->m_longlat_bbox.min());
      Vector2 northeast = out.lonlat_to_point(m_data->m_longlat_bbox.max());
      BBox2i utm_bbox(/* west */ southwest.x(),
                      /* south */ southwest.y(),
                      /* width */ northeast.x() - southwest.x(),
                      /* height */ northeast.y() - southwest.y()
                      );

      Vector2i dims = qtree.get_dimensions();
      double aspect_ratio = 2 * (region.width()/region.height()) * ( (utm_bbox.width()/dims.x()) / (utm_bbox.height()/dims.y()) );

      printf("name=%s utm=%d %d %d %d\n", name.c_str(), utm_bbox.min().x(), utm_bbox.min().y(), utm_bbox.max().x(), utm_bbox.max().y());

      children.push_back( std::make_pair( name + "0", BBox2i( (region + region.min()) / 2 ) ) );
      children.push_back( std::make_pair( name + "1", BBox2i( (region + Vector2i(region.max().x(),region.min().y())) / 2 ) ) );
      children.push_back( std::make_pair( name + "2", BBox2i( (region + Vector2i(region.min().x(),region.max().y())) / 2 ) ) );
      children.push_back( std::make_pair( name + "3", BBox2i( (region + region.max()) / 2 ) ) );
    }
    return children;
  }

} // namespace mosaic
} // namespace vw
