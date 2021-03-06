/////////////////////////////////////////////////////////////////////////////
//
// Identisnap
// Vision engine
//
/////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <string>
#include "engine.hpp"

using namespace cv;

/* Create a feature point detector.
**    In:   type (SIFT or SURF)
**    Out:  detector
*/
void createDetector(Ptr<FeatureDetector> &detector, std::string type)
{
  if(type.compare("SURF") == 0)
  {
    detector = xfeatures2d::SURF::create();
  } else if(type.compare("SIFT") == 0 || type.compare("ROOTSIFT") == 0) {
    detector = xfeatures2d::SIFT::create();
  } else {
    printf("Invalid detector type!\n");
  }
}

/* Use a feature detector to calculate keypoints and
** descriptors for the input image.
*/
/* Single image.
**  In:   image
**  Out:  keypoints, descriptors
*/
void getKeypointsAndDescriptors(Mat &image, std::vector<KeyPoint> &keypoints, Mat &descriptors, Ptr<FeatureDetector> &detector)
{
  detector->detectAndCompute(image, noArray(), keypoints, descriptors, false);
}
/* Multiple images.
**  In:   images
**  Out:  keypoints, descriptors
*/
void getKeypointsAndDescriptors(std::vector<Mat> &images, std::vector<std::vector<KeyPoint> > &keypoints, std::vector<Mat> &descriptors, Ptr<FeatureDetector> &detector)
{
  detector->detect(images, keypoints);
  detector->compute(images, keypoints, descriptors);
}
/* Single query image, multiple training images.
**  In:   queryImage, trainingImages, detector
**  Out:  queryKeypoints, queryDescriptors, trainingKeypoints, trainingDescriptors
*/
void getKeypointsAndDescriptors(Mat &queryImage, std::vector<KeyPoint> &queryKeypoints, Mat &queryDescriptors,
  std::vector<Mat> &trainingImages, std::vector<std::vector<KeyPoint> > &trainingKeypoints, std::vector<Mat> &trainingDescriptors,
  Ptr<FeatureDetector> &detector)
{
  detector->detect(queryImage, queryKeypoints);
  detector->detect(trainingImages, trainingKeypoints);
  detector->compute(queryImage, queryKeypoints, queryDescriptors);
  detector->compute(trainingImages, trainingKeypoints, trainingDescriptors);
}

// Compute the RootSIFT from SIFT according to Arandjelovic and Zisserman
// https://alufr-ros-pkg.googlecode.com/svn/trunk/rgbdslam_freiburg/rgbdslam/src/node.cpp
void rootSIFT(cv::Mat& descriptors)
{
  // Compute sums for L1 Norm
  Mat sums_vec;
  descriptors = abs(descriptors); //otherwise we draw sqrt of negative vals
  reduce(descriptors, sums_vec, 1 /*sum over columns*/, CV_REDUCE_SUM, CV_32FC1);
  for(unsigned int row = 0; row < descriptors.rows; row++) {
    int offset = row*descriptors.cols;
    for(unsigned int col = 0; col < descriptors.cols; col++) {
      descriptors.at<float>(offset + col) = sqrt(descriptors.at<float>(offset + col) / sums_vec.at<float>(row) /*L1-Normalize*/);
    }
  }
}

/* Filter a set of matches by thresholding at twice the minimum distance,
** (i.e. twice the best-match distance)
**
**    In:   queryDescriptors
**    Out:  matches
*/
void simpleFilter(Mat &queryDescriptors, std::vector<DMatch> &matches)
{
  //Calculate best match distance
  double min_dist = matches[0].distance;
  for(int i = 0; i < queryDescriptors.rows; i++)
  {
    double dist = matches[i].distance;
    if(dist < min_dist) min_dist = dist;
  }

  // Draw only "good" matches (i.e. whose distance is less than 2*min_dist,
  // or a small arbitary value ( 0.02 ) in the event that min_dist is very
  // small)
  std::vector<DMatch> good_matches;
  for(int i = 0; i < queryDescriptors.rows; i++)
  {
    if(matches[i].distance <= max(2*min_dist, 0.02))
    {
      good_matches.push_back(matches[i]);
    }
  }
  matches = good_matches;
}

/* Filter a set of knn matches according to Lowe's
** nearest neighbour distance ratio.
**
**    In:   knnMatches
**    Out:  matches
*/
void loweFilter(std::vector<std::vector<DMatch> > &knnMatches, std::vector<DMatch> &matches)
{
  std::vector<DMatch> good_matches;
  int count = 0;
  for (int i = 0; i < knnMatches.size(); i++)
  {
    const float ratio = 0.8; // 0.8 in Lowe's paper; can be tuned
    if (knnMatches[i][0].distance <= ratio * knnMatches[i][1].distance)
    {
      good_matches.push_back(knnMatches[i][0]);
    } else {
      count++;
    }
  }
  matches = good_matches;
}


/* Filter a set of matches by computing the homography matrix describing the transform
** of the query keypoints onto the training keypoints. Internally, this computation uses
** RANSAC to find inliers, and inlier matches are the ones which pass through the filter.
*/
/* 1D vector of query and training keypoints.
**
**    In:   matches, queryKeypoints, trainingKeypoints
**    Out:  matches, homography
*/
void ransacFilter(std::vector<DMatch> &matches, std::vector<KeyPoint> &queryKeypoints, std::vector<KeyPoint> &trainingKeypoints, Mat &homography)
{
  if(matches.size() < 4) return;  // cannot compute homography with < 4 points
  std::vector<Point2f> queryCoords;
  std::vector<Point2f> trainingCoords;

  //Get the coords of the keypoints from the matches
  for(int i = 0; i < matches.size(); i++)
  {
    queryCoords.push_back(queryKeypoints.at(matches.at(i).queryIdx).pt);
    trainingCoords.push_back(trainingKeypoints.at(matches.at(i).trainIdx).pt);
  }

  // Keep the inlier matches
  Mat outputMask;
  homography = findHomography(queryCoords, trainingCoords, CV_RANSAC, 3, outputMask);
  int inlierCounter = 0;
  std::vector<DMatch> good_matches;
  for(int i = 0; i < outputMask.rows; i++)
  {
    if((unsigned int)outputMask.at<uchar>(i))
    {
      inlierCounter++;
      good_matches.push_back(matches[i]);
    }
  }
  matches = good_matches;
}

/* 1D vector of query keypoints, a set of training keypoint vectors,
** each usually corresponding to its own training image.
** If no matches are found for a set of training keypoints, the identity
** matrix is pushed onto the homographies list.
**
**    In:   matches, queryKeypoints, trainingKeypoints
**    Out:  matches, homographies
*/
void ransacFilter(std::vector<DMatch> &matches, std::vector<KeyPoint> &queryKeypoints, std::vector<std::vector<KeyPoint> > &trainingKeypoints, std::vector<Mat> &homographies)
{
  std::vector<Point2f> queryCoords;
  std::vector<Point2f> trainingCoords;
  std::vector<DMatch> good_matches;

  for(int j = 0; j < trainingKeypoints.size(); j++)
  {
    queryCoords.clear();
    trainingCoords.clear();

    for( int i = 0; i < matches.size(); i++ )
    {
      //Get the coords of the keypoints from the matches, making sure to
      //only consider matches derived from keypoints in this training image
      if(matches.at(i).imgIdx == j)
      {
        queryCoords.push_back( queryKeypoints.at(matches.at(i).queryIdx).pt );
        trainingCoords.push_back( trainingKeypoints.at(j).at(matches.at(i).trainIdx).pt );
      }
    }

    Mat outputMask;
    int inlierCounter = 0;

    if(queryCoords.size() != 0 && queryCoords.size() != 0)
    {
      homographies.push_back(findHomography(queryCoords, trainingCoords, CV_RANSAC, 3, outputMask));
    } else {
      //cant compute homography is there were no matches in this training image, so push identity matrix
      homographies.push_back(Mat::eye(3, 3, CV_64F));
    }

    //Filter matches according to RANSAC inliers
    for (int i = 0; i < outputMask.rows; i++) {
      if((unsigned int)outputMask.at<uchar>(i)) {
        inlierCounter++;
        good_matches.push_back(matches[i]);
      }
    }
  }
  matches = good_matches;
}

/* Consider the bounding box of input image as the object.
** Transform this box according to the homography matrix and draw it on the
** output image.
*/
void drawProjection(Mat &input, Mat &homography, Mat &output)
{
  std::vector<Point2f> objCorners(4);
  objCorners[0] = Point(0,0);
  objCorners[1] = Point( input.cols, 0 );
  objCorners[2] = Point( input.cols, input.rows );
  objCorners[3] = Point( 0, input.rows );

  std::vector<Point2f> scnCorners(4);

  perspectiveTransform(objCorners, scnCorners, homography);

  line( output, scnCorners[0] + Point2f( input.cols, 0), scnCorners[1] + Point2f( input.cols, 0), Scalar(0, 255, 0), 4);
  line( output, scnCorners[1] + Point2f( input.cols, 0), scnCorners[2] + Point2f( input.cols, 0), Scalar( 0, 255, 0), 4);
  line( output, scnCorners[2] + Point2f( input.cols, 0), scnCorners[3] + Point2f( input.cols, 0), Scalar( 0, 255, 0), 4);
  line( output, scnCorners[3] + Point2f( input.cols, 0), scnCorners[0] + Point2f( input.cols, 0), Scalar( 0, 255, 0), 4);
}

/*
** Transform this box of the object according to the homography matrix, calculate the
** area of the projected shape, and workout the ratio:
** area of object/area of projection
*/
double calcProjectedAreaRatio(std::vector<Point2f> &objCorners, Mat &homography)
{
  std::vector<Point2f> scnCorners(4);

  perspectiveTransform(objCorners, scnCorners, homography);

  return contourArea(scnCorners)/contourArea(objCorners);
}


void getFilteredMatches(Mat &image1, std::vector<KeyPoint> &keypoints1, Mat &descriptors1, std::vector<KeyPoint> &keypoints2, Mat &descriptors2, std::vector<DMatch> &matches)
{
  // Match query and viewpoint
  Ptr<FlannBasedMatcher> matcher = new FlannBasedMatcher();
  std::vector<std::vector<DMatch> > knn_matches;
  matches.clear();
  matcher->knnMatch(descriptors1, descriptors2, knn_matches, 2);
  loweFilter(knn_matches, matches);

  // Perform geometric verification
  if(matches.size() > 4) {
    // RANSAC filter
    Mat homography;
    ransacFilter(matches, keypoints1, keypoints2, homography);

    // if a homography was successfully computed...
    if(homography.cols != 0 && homography.rows != 0)
    {
      std::vector<Point2f> objCorners(4);
      objCorners[0] = Point(0,0);
      objCorners[1] = Point( image1.cols, 0 );
      objCorners[2] = Point( image1.cols, image1.rows );
      objCorners[3] = Point( 0, image1.rows );
      double area = calcProjectedAreaRatio(objCorners, homography);
      // do not count these matches if projected area too small, (likely
      // mapping to single point => erroneous matching)
      if(area < 0.0005)
      {
        matches.clear();
      }
    }
  }
}
