#include "panorama.h"
#include "matrix.h"
#include <unistd.h>
#include <ctime>

using namespace std;

Image computeTensor(const Image &im, float sigmaG, float factorSigma) {
    // // --------- HANDOUT  PS07 ------------------------------
    // Compute xx/xy/yy Tensor of an image. (stored in that order)
    vector<Image> lumi_chromi = lumiChromi(im);
    Image lumi = lumi_chromi[0];
    Image chromi = lumi_chromi[1];
    //Using a Gaussian with standard deviation sigmaG, blur the luminance
    //to control the scale at which corners are extracted. A little bit
    //of blur helps smooth things out and help extract stable mid-scale corners.
    Image blurred_lumi = gaussianBlur_separable(lumi, sigmaG);
    Image gradientX_lumi = gradientX(blurred_lumi);
    Image gradientY_lumi = gradientY(blurred_lumi);

    //Structure tensor image
    //Where channel 0 is Ix2
    //and channel 1 is IxIy
    //Channel 2 is Iy2
    Image perPixelContributions(im.width(), im.height(), 3);

    for (int i = 0; i < im.width(); i++){
        for (int j = 0; j < im.height(); j++){
            perPixelContributions(i,j,0) = pow(gradientX_lumi(i,j),2);
            perPixelContributions(i,j,1) = gradientX_lumi(i,j)*gradientY_lumi(i,j);
            perPixelContributions(i,j,2) = pow(gradientY_lumi(i,j),2);
        }
    }

    Image structure_tensor = gaussianBlur_separable(perPixelContributions, sigmaG*factorSigma);

    return structure_tensor;
}

Image cornerResponse(const Image &im, float k, float sigmaG, 
        float factorSigma) 
{
    // // --------- HANDOUT  PS07 ------------------------------
    // Compute response = det(M) - k*[(trace(M)^2)] at every pixel location,
    // using the structure tensor of im.
    Image structure_tensor = computeTensor(im, sigmaG, factorSigma);
    Image corner_response(im.width(), im.height(), 1); 

    for (int i = 0; i < structure_tensor.width(); i++){
        for (int j = 0; j < structure_tensor.height(); j++){
            Matrix M = Matrix::Zero(2,2);
            M(0,0) = structure_tensor(i,j,0);
            M(0,1) = structure_tensor(i,j,1);
            M(1,0) = structure_tensor(i,j,1);
            M(1,1) = structure_tensor(i,j,2);
            float R = M.determinant() - k*pow(M.trace(), 2);
            if (R > 0) {
                corner_response(i, j) = R;
            }
        }
    }

    return corner_response;
}


vector<Point> HarrisCorners(const Image &im, float k, float sigmaG,
        float factorSigma, float maxiDiam, float boundarySize) 
{
    // // --------- HANDOUT  PS07 ------------------------------
    // Compute Harris Corners by maximum filtering the cornerResponse map.
    // The corners are the local maxima.
    vector<Point> harris_corners;
    Image corner_response = cornerResponse(im, k, sigmaG, factorSigma);
    corner_response.write("./Output/harris_corners_corner_response.png");
    Image maximum_corner_response = maximum_filter(corner_response, maxiDiam);
    maximum_corner_response.write("./Output/maximum_corner_response.png");
    //exclude boundary corners
    for (int i = boundarySize; i < (corner_response.width() - boundarySize); i++){
        for (int j = boundarySize; j < (corner_response.height() - boundarySize); j++){
            if (maximum_corner_response(i,j) > 0 && corner_response(i,j) == maximum_corner_response(i,j)) {
                harris_corners.push_back(Point(i,j));
            }
        }
    }
    return harris_corners;
}


Image descriptor(const Image &blurredIm, Point p, float radiusDescriptor) {
    // // --------- HANDOUT  PS07 ------------------------------
    // Extract a descriptor from blurredIm around point p, with a radius 'radiusDescriptor'.

    Image output(radiusDescriptor*2+1, radiusDescriptor*2+1, 1);
    for (int i = p.x - radiusDescriptor; i < p.x + radiusDescriptor + 1; i++){
        for (int j = p.y - radiusDescriptor; j < p.y + radiusDescriptor + 1; j++){
            output(i - (p.x - radiusDescriptor), j - (p.y - radiusDescriptor)) = blurredIm(i, j);
        }
    }
    //subtracting the mean
    Image subtracted_output = output;
    for (int i = 0; i < output.width(); i++){
        for (int j = 0; j < output.height(); j++){
            subtracted_output(i,j) = output(i, j) - output.mean();
        }
    }
    Image final_output = subtracted_output;
    for (int i = 0; i < subtracted_output.width(); i++){
        for (int j = 0; j < subtracted_output.height(); j++){
            final_output(i,j) = subtracted_output(i,j)/pow(subtracted_output.var(), 0.5);
        }
    }
    return final_output;
}


vector <Feature> computeFeatures(const Image &im, vector<Point> cornersL,
    float sigmaBlurDescriptor, float radiusDescriptor) {
    // // --------- HANDOUT  PS07 ------------------------------
    // Pset07. obtain corner features from a list of corner points
    
    vector <Feature> features;
    vector<Image> lumi_chromi = lumiChromi(im);
    Image lumi = lumi_chromi[0];
    //Using a Gaussian with standard deviation sigmaG, blur the luminance
    //to control the scale at which corners are extracted. A little bit
    //of blur helps smooth things out and help extract stable mid-scale corners.
    Image blurred_lumi = gaussianBlur_separable(lumi, sigmaBlurDescriptor);

    for (Point point : cornersL){
        Image descriptor_for_point = descriptor(blurred_lumi, point, radiusDescriptor);
        Feature new_feature(point, descriptor_for_point);
        features.push_back(new_feature);
    }

    return features;
}



float l2Features(Feature &f1, Feature &f2) {
    // // --------- HANDOUT  PS07 ------------------------------
    // Compute the squared Euclidean distance between the descriptors of f1, f2.
    float dist = 0;

    for (int i = 0; i < f1.desc().width(); i++){
        for (int j = 0; j < f1.desc().height(); j++){
            dist += pow(f1.desc()(i,j) - f2.desc()(i,j), 2);
        }
    }

    return dist;
}


vector <FeatureCorrespondence> findCorrespondences(vector<Feature> listFeatures1, vector<Feature> listFeatures2, float threshold) {
    // // --------- HANDOUT  PS07 ------------------------------
    // Find correspondences between listFeatures1 and listFeatures2 using the
    // second-best test.

    vector <FeatureCorrespondence> correspondences;
    float threshold_squared = pow(threshold, 2);
    for (Feature f1: listFeatures1){
        float best_dif = 5000000;
        Feature best_feature = f1;
        
        float second_best_dif = 50000;
        Feature second_best_feature = f1;

        bool was_set = false;

        for (Feature f2: listFeatures2){
            float dif = l2Features(f1, f2);
            //cout << "Examining best dif: " << best_dif << " and dif: " << dif << " where best_dif < best is: " << best_dif < best << " and " << endl; 
            //std::cout << best_dif/dif << " and threshold_squared is: " << threshold_squared << endl;
            if (dif < best_dif){
                //cout << "made it into first level" << endl;
                //cout << "Examining best dif: " << best_dif << " and second best dif: " << second_best_dif << " where second_best_dif/best_dif is: " << second_best_dif/best_dif << endl;                 
                was_set = true;

                second_best_dif = best_dif;
                second_best_feature = best_feature;
                
                best_dif = dif;
                best_feature = f2;
                //cout << "updating best feature" << endl;
            }
            else if (dif < second_best_dif) {
                second_best_dif = dif;
                second_best_feature = f2;                
            
            }
        }
        if (second_best_dif/best_dif >= threshold_squared && was_set == true) {
            correspondences.push_back(FeatureCorrespondence(f1, best_feature));
        }
    }

    return correspondences;
}


vector<bool> inliers(Matrix H, vector <FeatureCorrespondence> listOfCorrespondences, float epsilon) {
    // // --------- HANDOUT  PS07 ------------------------------
    // Pset07: Implement as part of RANSAC
    // return a vector of bools the same size as listOfCorrespondences indicating
    //  whether each correspondance is an inlier according to the homography H and threshold epsilon

    vector<bool> output;

    for (FeatureCorrespondence feature_corr: listOfCorrespondences){
        CorrespondencePair feature_corr_pair = feature_corr.toCorrespondencePair();
        Vec3f transformed_first_point = H*feature_corr_pair.point1;
        Vec3f diff = transformed_first_point - feature_corr_pair.point2;
        float mag = diff.norm();
        output.push_back(mag < epsilon);
    }

    return output;
}

Matrix RANSAC(vector <FeatureCorrespondence> listOfCorrespondences, int Niter, float epsilon) {
    // // --------- HANDOUT  PS07 ------------------------------
    // Put together the RANSAC algorithm.

    Matrix best_H = Matrix::Identity(3,3);
    int max_inliers = 0;

    for (int ransac_iter = 0; ransac_iter < Niter; ransac_iter++){
        
        vector<FeatureCorrespondence> random_corrs = sampleFeatureCorrespondences(listOfCorrespondences);
        vector<FeatureCorrespondence> listOfFeatureCorrespondences = {random_corrs[0], random_corrs[1], random_corrs[2], random_corrs[3]};
        vector<CorrespondencePair> listOfCorrespondencePairs = getListOfPairs(listOfFeatureCorrespondences);
        CorrespondencePair arrayOfCorrespondencePairs [4] = {listOfCorrespondencePairs[0], listOfCorrespondencePairs[1], listOfCorrespondencePairs[2], listOfCorrespondencePairs[3]};
        Matrix H = computeHomography(arrayOfCorrespondencePairs);
        
        //singular linear system
        if (H.determinant() == 0){
            H = Matrix::Identity(3,3);
        }

        vector<bool> inlier_vector = inliers(H, listOfFeatureCorrespondences, epsilon);
        int inlier_count = 0;

        for (bool inlier_bool : inlier_vector){
            if (inlier_bool){
                inlier_count += 1;
            }
        }

        if (inlier_count >= max_inliers) {
            max_inliers = inlier_count;
            best_H = H;
        }

    }

    return best_H;
}


Image autostitch(Image &im1, Image &im2, float blurDescriptor, float radiusDescriptor) {
    // // --------- HANDOUT  PS07 ------------------------------
    // Now you have all the ingredients to make great panoramas without using a
    // primitive javascript UI !
    vector<Point> corners_1 = HarrisCorners(im1);
    vector<Point> corners_2 = HarrisCorners(im2);
    vector<Feature> features_1 = computeFeatures(im1, corners_1, blurDescriptor, radiusDescriptor);
    vector<Feature> features_2 = computeFeatures(im2, corners_2, blurDescriptor, radiusDescriptor);
    vector <FeatureCorrespondence> listOfFeatureCorrespondences = findCorrespondences(features_1, features_2);
    Matrix H = RANSAC(listOfFeatureCorrespondences);

    BoundingBox B_1 = computeTransformedBBox(im1.width(), im1.height(), H);
    BoundingBox B_2 = computeTransformedBBox(im2.width(), im2.height(), Matrix::Identity(3,3));
    BoundingBox B = bboxUnion(B_1,B_2);
    Matrix T = makeTranslation(B);

    Image out(B.x2 - B.x1, B.y2 - B.y1, im1.channels());    
    applyHomographyFast(im2, T, out, true);
    applyHomographyFast(im1, T*H, out, true);

    return out;
}




// *****************************************************************************
//  * Helpful optional functions to implement
// ****************************************************************************

Image getBlurredLumi(const Image &im, float sigmaG) {
    return Image(1,1,1);
}

int countBoolVec(vector<bool> ins) {
    return 0;
}

// *****************************************************************************
//  * Do Not Modify Below This Point
// *****************************************************************************

// Pset07 RANsac helper. re-shuffle a list of correspondances
vector<FeatureCorrespondence> sampleFeatureCorrespondences(vector <FeatureCorrespondence> listOfCorrespondences) {
    random_shuffle(listOfCorrespondences.begin(), listOfCorrespondences.end());
    return listOfCorrespondences;
}

// Pset07 RANsac helper: go from 4 correspondances to a list of points [4][2][3] as used in Pset06.
// Note: The function uses the first 4 correspondances passed
vector<CorrespondencePair> getListOfPairs(vector <FeatureCorrespondence> listOfCorrespondences) {
    vector<CorrespondencePair> out;
    for (int i = 0; i < 4; i++) {
        out.push_back(listOfCorrespondences[i].toCorrespondencePair());
    }
    return out;
}

// Corner visualization.
Image visualizeCorners(const Image &im, vector<Point> pts, 
        int rad, const vector <float> & color) 
{
    Image vim = im;
    for (int i = 0; i < (int) pts.size(); i++) {
        int px = pts[i].x;
        int py = pts[i].y;

        int minx = max(px - rad, 0);

        for (int delx = minx; delx < min(im.width(), px + rad + 1); delx++) 
        for (int dely = max(py - rad, 0); dely < min(im.height(), py + rad + 1); dely++) 
        {
            if ( sqrt(pow(delx-px, 2) + pow(dely - py, 2)) <= rad) {
                for (int c = 0; c < im.channels(); c++) {
                    vim(delx, dely, c) = color[c];
                }
            }
        }
    }
    return vim;
}

Image visualizeFeatures(const Image &im, vector <Feature> LF, float radiusDescriptor) {
    // assumes desc are within image range
    Image vim = im;
    int rad = radiusDescriptor;

    for (int i = 0; i < (int) LF.size(); i++) {
        int px = LF[i].point().x;
        int py = LF[i].point().y;
        Image desc = LF[i].desc();

        for (int delx = px - rad; delx < px + rad + 1; delx++) {
            for (int dely = py - rad; dely < py + rad + 1; dely++) {
                vim(delx, dely, 0) = 0;
                vim(delx, dely, 1) = 0;
                vim(delx, dely, 2) = 0;

                if (desc(delx - (px-rad), dely - (py - rad)) > 0) {
                    vim(delx, dely, 1) = 1;
                } else if (desc(delx - (px-rad), dely - (py - rad)) < 0) {
                    vim(delx, dely, 0) = 1;
                }
            }
        }
    }
    return vim;
}

void drawLine(Point p1, Point p2, Image &im,  const vector<float> & color) {
    float minx = min(p1.x, p2.x);
    float miny = min(p1.y, p2.y);
    float maxx = max(p1.x, p2.x);
    float maxy = max(p1.y, p2.y);

    int spaces = 1000;
    for (int i = 0; i < spaces; i++) {
        float x = minx + (maxx - minx) / spaces * (i+1);
        float y = miny + (maxy - miny) / spaces * (i+1);
        for (int c = 0; c < im.channels(); c++) {
            im(x, y, c) = color[c];
        }
    }
}

Image visualizePairs(const Image &im1, const Image &im2, vector<FeatureCorrespondence> corr) {
    Image vim(im1.width() + im2.width(), im1.height(), im1.channels());

    // stack the images
    for (int j = 0; j < im1.height(); j++) {
        for (int c = 0; c < im1.channels(); c++) {
            for (int i = 0; i < im1.width(); i++) {
                vim(i,j,c) = im1(i,j,c);
            }
            for (int i = 0; i < im2.width(); i++) {
                vim(i+im1.width(),j,c) = im2(i,j,c);
            }
        }
    }

    // draw lines
    for (int i = 0; i < (int) corr.size(); i++) {
        Point p1 = corr[i].feature(0).point();
        Point p2 = corr[i].feature(1).point();
        p2.x = p2.x + im1.width();
        drawLine(p1, p2, vim);
    }
    return vim;
}

Image visualizePairsWithInliers(const Image &im1, const Image &im2, vector<FeatureCorrespondence> corr, const vector<bool> & ins) {
    Image vim(im1.width() + im2.width(), im1.height(), im1.channels());

    // stack the images
    for (int j = 0; j < im1.height(); j++) {
        for (int c = 0; c < im1.channels(); c++) {
            for (int i = 0; i < im1.width(); i++) {
                vim(i,j,c) = im1(i,j,c);
            }
            for (int i = 0; i < im2.width(); i++) {
                vim(i+im1.width(),j,c) = im2(i,j,c);
            }
        }
    }

    // draw lines
    vector<float> red(3,0);
    vector<float> green(3,0);
    red[0] = 1.0f;
    green[1]= 1.0f;

    for (int i = 0; i < (int) corr.size(); i++) {
        Point p1 = corr[i].feature(0).point();
        Point p2 = corr[i].feature(1).point();
        p2.x = p2.x + im1.width();
        if (ins[i]) {
            drawLine(p1, p2, vim, green);
        } else {
            drawLine(p1, p2, vim, red);
        }
    }
    return vim;

}

// Inliers:  Detected corners are in green, reprojected ones are in red
// Outliers: Detected corners are in yellow, reprojected ones are in blue
vector<Image> visualizeReprojection(const Image &im1, const Image &im2, Matrix  H, vector<FeatureCorrespondence> & corr, const vector<bool> & ins) {
    // Initialize colors
    vector<float> red(3,0);
    vector<float> green(3,0);
    vector<float> blue(3,0);
    vector<float> yellow(3,0);
    red[0] = 1.0f;
    green[1]= 1.0f;
    blue[2] = 1.0f;
    yellow[0] = 1.0f;
    yellow[1] = 1.0f;

    vector<Point> detectedPts1In;
    vector<Point> projectedPts1In;
    vector<Point> detectedPts1Out;
    vector<Point> projectedPts1Out;

    vector<Point> detectedPts2In;
    vector<Point> projectedPts2In;
    vector<Point> detectedPts2Out;
    vector<Point> projectedPts2Out;

    for (int i = 0 ; i < (int) corr.size(); i++) {
        Point pt1 = corr[i].feature(0).point();
        Point pt2 = corr[i].feature(1).point();
        Matrix P1 = pt1.toHomogenousCoords();
        Matrix P2 = pt2.toHomogenousCoords();
        Matrix P2_proj = H*P1;
        Matrix P1_proj = H.inverse()*P2;
        Point reproj1 = Point(P1_proj(0)/P1_proj(2), P1_proj(1)/P1_proj(2));
        Point reproj2 = Point(P2_proj(0)/P2_proj(2), P2_proj(1)/P2_proj(2));
        if (ins[i]) { // Inlier
            detectedPts1In.push_back(pt1);
            projectedPts1In.push_back(reproj1);
            detectedPts2In.push_back(pt2);
            projectedPts2In.push_back(reproj2);
        } else { // Outlier
            detectedPts1Out.push_back(pt1);
            projectedPts1Out.push_back(reproj1);
            detectedPts2Out.push_back(pt2);
            projectedPts2Out.push_back(reproj2);
        }
    }

    vector<Image> output;
    Image vim1(im1);
    Image vim2(im2);
    vim1 = visualizeCorners(im1, detectedPts1In,2, green);
    vim1 = visualizeCorners(vim1, projectedPts1In,1, red);
    vim1 = visualizeCorners(vim1, detectedPts1Out,2, yellow);
    vim1 = visualizeCorners(vim1, projectedPts1Out,1, blue);

    vim2 = visualizeCorners(im2, detectedPts2In,2, green);
    vim2 = visualizeCorners(vim2, projectedPts2In,1, red);
    vim2 = visualizeCorners(vim2, detectedPts2Out,2, yellow);
    vim2 = visualizeCorners(vim2, projectedPts2Out,1, blue);


    output.push_back(vim1);
    output.push_back(vim2);
    return output;
}



/***********************************************************************
 * Point and Feature Definitions *
 **********************************************************************/
Point::Point(int xp, int yp) : x(xp), y(yp) {}
Point::Point() : x(0.0f), y(0.0f) {}
void Point::print() { printf("(%d, %d)\n", x, y); }
Vec3f Point::toHomogenousCoords() {
    Vec3f b;
    b(0) = x;
    b(1) = y;
    b(2) = 1;
    return b;
}

// Feature Constructors
Feature::Feature(Point ptp, const Image &descp) 
    : pt(ptp), dsc(descp)
{
    pt = ptp;
    dsc = descp;
}

// getter functions
Point Feature::point() { return pt;}
Image Feature::desc() { return dsc;}

// printer
void Feature::print() {
    printf("Feature:");
    point().print();
    for (int j = 0; j < dsc.height(); j++) {
        for (int i = 0; i < dsc.width(); i++) {
            printf("%+07.2f ", dsc(i, j));
        }
        printf("\n");
    }
}

// FeatureCorrespondence Constructors
FeatureCorrespondence::FeatureCorrespondence(const Feature &f1p, const Feature &f2p) 
    : f1(f1p), f2(f2p)
{
}


vector<Feature> FeatureCorrespondence::features() {
    vector<Feature> feats;
    feats.push_back(f1);
    feats.push_back(f2);
    return feats;
}


Feature FeatureCorrespondence::feature(int i) {
    if (i == 0)
        return f1;
    else
        return f2;
}


// printer
void FeatureCorrespondence::print() {
    printf("FeatureCorrespondence:");
    f1.print();
    f2.print();
}


CorrespondencePair FeatureCorrespondence::toCorrespondencePair() {

    return CorrespondencePair(
        (float) f1.point().x,
        (float) f1.point().y,
        1,
        (float) f2.point().x,
        (float) f2.point().y,
        1
    );
}


