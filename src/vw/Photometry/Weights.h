// __BEGIN_LICENSE__
// Copyright (C) 2006-2010 United States Government as represented by
// the Administrator of the National Aeronautics and Space Administration.
// All Rights Reserved.
// __END_LICENSE__


/// \file Weights.h

#ifndef __VW_PHOTOMETRY_WEIGHTS_H__
#define __VW_PHOTOMETRY_WEIGHTS_H__

#include <string>

#include <vw/Math/Vector.h>
#include <vw/Core.h>
#include <vw/Image.h>
#include <vw/FileIO.h>
#include <vw/Cartography.h>
#include <vw/Math.h>

namespace vw {
namespace photometry {
    
    class FeatherWeights {
    public:
        FeatherWeights(bool isDem_=false, float noDataVal_=0)
            : initialized(false),
              isDem(isDem_),
              noDataVal(noDataVal_)
        {}

        bool isInitialized(void) const {
            return initialized;
        }

        float getWeight(const Vector2i& pix) const {
            return weights(pix[0], pix[1])/255.0;
        }

        void init(const std::string& imageFile,
                  const std::string& weightsFile) {
            if (initialized) return;

            if (isDem) {
                DiskImageView<PixelGrayA<float> > galphaOutput(weightsFile);
                weights = select_alpha_channel(galphaOutput);
            } else {
                DiskImageView<PixelGrayA<uint8> > galphaOutput(weightsFile);
                weights = select_alpha_channel(galphaOutput);
            }
            initialized = true;
        }

    protected:
        bool initialized;
        bool isDem;
        float noDataVal;
        ImageView<PixelGray<uint8> > weights;
    };

    inline void initWeights(const std::string& drgWeightsFname,
                            const std::string& demWeightsFname,
                            const std::string& drgImgFname,
                            const std::string& demImgFname,
                            FeatherWeights* drgWeights,
                            FeatherWeights* demWeights,
                            bool saveWeights)
    {
        drgWeights->init(drgImgFname, drgWeightsFname);
        demWeights->init(demImgFname, demWeightsFname);
    }

}} // end vw::photometry

#endif//__VW_PHOTOMETRY_WEIGHTS_H__
