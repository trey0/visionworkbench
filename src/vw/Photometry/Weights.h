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

    class DrgValidChecker {
    public:
        bool operator()(const ImageView<PixelMask<PixelGray<uint8> > >& img, int x, int y) {
            return is_valid(img(x, y));
        }
    };

    class DemValidChecker {
    public:
        float noDataVal;

        DemValidChecker(void) { assert(false); /* don't call this */ }
        DemValidChecker(float noDataVal_) : noDataVal(noDataVal_) {}
        bool operator()(const ImageView<PixelGray<float> >& img, int x, int y) {
            return img(x, y) != noDataVal;
        }
    };
    
    template <class PixelT, class ValidChecker>
    class FeatherWeights {
    public:
        FeatherWeights(const ValidChecker& isValid_=ValidChecker())
            : initialized(false), isValid(isValid_)
        {}

        bool isInitialized(void) const {
            return initialized;
        }

        float getWeight(const Vector2i& pix) const {
            return getWeightH(pix) * getWeightV(pix);
        }

        float getWeightV(const Vector2i& pix) const {
            int x = pix[0];
            int y = pix[1];
            int radius = vradius[y];
            if (radius == 0) {
                return 0;
            } else {
                float dist = fabs(x - vcenter[y]);
                return std::max(0.0f, 1 - dist/radius);
            }
        }

        float getWeightH(const Vector2i& pix) const {
            int x = pix[0];
            int y = pix[1];
            int radius = hradius[x];
            if (radius == 0) {
                return 0;
            } else {
                float dist = fabs(y - hcenter[x]);
                return std::max(0.0f, 1 - dist/radius);
            }
        }

        void computeFromImage(const std::string& img_file) {
            DiskImageView<PixelT> img(img_file);
            calcParamsH(img);
            calcParamsV(img);
            initialized = true;
        }

        void writeToFile(FILE* fp) const {
            fprintf(fp, "%d %d\n", (int)hcenter.size(), (int)vcenter.size());
            writeVec(fp, hcenter);
            writeVec(fp, hradius);
            writeVec(fp, vcenter);
            writeVec(fp, vradius);
        }

        void readFromFile(FILE* fp) {
            int rows, cols;
            assert(2 == fscanf(fp, "%d %d\n", &cols, &rows));
            readVec(fp, hcenter, cols);
            readVec(fp, hradius, cols);
            readVec(fp, vcenter, rows);
            readVec(fp, vradius, rows);
            initialized = true;
        }

    protected:
        bool initialized;
        ValidChecker isValid;
        // for row y, column x:
        // * hcenter[x] is the center y value of the valid data in column x
        // * hradius[x] is half the height of the valid data in column x
        // * vcenter[y] is the center x value of the valid data in row y
        // * vradius[y] is half the width of the valid data in row y
        std::vector<int> hcenter, vcenter, hradius, vradius;
        
        void writeVec(FILE* fp, const std::vector<int>& vec) const {
            for (int i=0; i < (int)vec.size(); i++) {
                fprintf(fp, "%d ", vec[i]);
            }
            fprintf(fp, "\n");
        }

        void readVec(FILE* fp, std::vector<int>& vec, int numEntries) {
            vec.resize(numEntries, 0);
            for (int i=0; i < numEntries; i++) {
                assert(1 == fscanf(fp, "%d ", &vec[i]));
            }
            fscanf(fp, "\n");
        }

        // initialize hcenter, hradius
        void calcParamsH(const ImageView<PixelT>& img) {
            hcenter.resize(img.cols(), 0);
            hradius.resize(img.cols(), 0);

            int minVal, maxVal;

            for (int x = 0; x < (int)img.cols(); ++x) {

                minVal = img.rows();
                maxVal = 0;
                for (int y = 0; y < (int)img.rows(); ++y) {
                    if ( isValid(img, x, y) ) {
                        
                        if (y < minVal){
                            minVal = y;
                        }
                        if (y > maxVal){
                            maxVal = y;
                        }
                    }
                }
                hcenter[x] = (minVal + maxVal)/2;
                hradius[x] = (maxVal - minVal)/2;
                if (hradius[x] < 0){
                    hradius[x] = 0;
                }
            }
        }
        
        // initialize vcenter, vradius
        void calcParamsV(const ImageView<PixelT>& img) {
            vcenter.resize(img.rows(), 0);
            vradius.resize(img.rows(), 0);
            
            int minVal, maxVal;
            
            for (int y = 0 ; y < (int)img.rows(); ++y) {
                
                minVal = img.cols();
                maxVal = 0;
                for (int x = 0; x < (int)img.cols(); ++x) {
                    if ( isValid(img, x, y) ) {
                        
                        if (x < minVal){
                            minVal = x;
                        }
                        if (x > maxVal){
                            maxVal = x;
                        }
                    }
                }
                vcenter[y] = (minVal + maxVal)/2;
                vradius[y] = (maxVal - minVal)/2;
                if (vradius[y] < 0){
                    vradius[y] = 0;
                }
            }
        }
    };

    typedef FeatherWeights<PixelMask<PixelGray<uint8> >, DrgValidChecker> DrgFeatherWeights;
    typedef FeatherWeights<PixelGray<float>, DemValidChecker> DemFeatherWeights;

    inline void writeWeights(const std::string& fname,
                             const DrgFeatherWeights* drgWeights,
                             const DemFeatherWeights* demWeights)
    {
        FILE* fp = fopen(fname.c_str(), "w");
        if (fp == NULL){
            fprintf(stderr, "ERROR: writeToFile: couldn't open %s for writing: %s\n",
                    fname.c_str(), strerror(errno));
            exit(1);
        }

        drgWeights->writeToFile(fp);
        demWeights->writeToFile(fp);

        fclose(fp);
    }

    inline bool readWeights(const std::string& fname,
                            DrgFeatherWeights* drgWeights,
                            DemFeatherWeights* demWeights)
    {
        FILE* fp = fopen(fname.c_str(), "r");
        if (fp == NULL){
            fprintf(stderr, "readWeights: couldn't open %s for reading: %s\n",
                    fname.c_str(), strerror(errno));
            return false;
        }

        drgWeights->readFromFile(fp);
        demWeights->readFromFile(fp);

        fclose(fp);

        return true;
    }

    inline void initWeights(const std::string& weightsFname,
                            const std::string& drgImgFname,
                            const std::string& demImgFname,
                            DrgFeatherWeights* drgWeights,
                            DemFeatherWeights* demWeights,
                            bool saveWeights)
    {
        if (!drgWeights->isInitialized()) {
            if (!readWeights(weightsFname, drgWeights, demWeights)) {
                drgWeights->computeFromImage(drgImgFname);
                demWeights->computeFromImage(demImgFname);
                if (saveWeights) {
                    writeWeights(weightsFname, drgWeights, demWeights);
                }
            }
        }
    }

}} // end vw::photometry

#endif//__VW_PHOTOMETRY_WEIGHTS_H__
