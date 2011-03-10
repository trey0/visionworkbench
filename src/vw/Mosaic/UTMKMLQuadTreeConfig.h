// __BEGIN_LICENSE__
// Copyright (C) 2006-2010 United States Government as represented by
// the Administrator of the National Aeronautics and Space Administration.
// All Rights Reserved.
// __END_LICENSE__


/// \file UTMKMLQuadTreeConfig.h
///
/// A configuration class that provides callbacks for
/// QuadTreeGenerator that generate multi-resolution
/// UTMKML overlays.
///
#ifndef __VW_MOSAIC_UTMKMLQUADTREECONFIG_H__
#define __VW_MOSAIC_UTMKMLQUADTREECONFIG_H__

#include <vw/Cartography/GeoReference.h>
#include <vw/Mosaic/QuadTreeGenerator.h>
#include <vw/Mosaic/QuadTreeConfig.h>
#include <vw/Mosaic/KMLQuadTreeConfig.h>

namespace vw {
namespace mosaic {

  struct UTMKMLQuadTreeConfigData;

  class UTMKMLQuadTreeConfig : public KMLQuadTreeConfig {
  public:
    UTMKMLQuadTreeConfig(void);
    virtual ~UTMKMLQuadTreeConfig() {}

    void set_output_utm_zone(int32 output_utm_zone) {
        m_output_utm_zone = output_utm_zone;
    }

    void configure( QuadTreeGenerator& qtree ) const;

    cartography::GeoReference output_georef(uint32 xresolution, uint32 yresolution = 0);
    cartography::GeoReference get_output_georef(uint32 xresolution, uint32 yresolution) const;
    cartography::GeoReference get_output_georef() const;

    std::vector<std::pair<std::string,vw::BBox2i> > branch_func( QuadTreeGenerator const&, std::string const& name, BBox2i const& region ) const;

  protected:
    int32 m_output_utm_zone;
    uint32 m_xresolution;
    uint32 m_yresolution;
  };

} // namespace mosaic
} // namespace vw

#endif // __VW_MOSAIC_UTMKMLQUADTREECONFIG_H__
