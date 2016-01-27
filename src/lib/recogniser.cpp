// header inclusion
#include <stdio.h>
#include "recogniser.hpp"

using namespace cv;
using namespace boost::python;

Recogniser::Recogniser(const char* _filename)
{
  filename = _filename;
  printf("Creating detector...\n");
  createDetector(detector, "SIFT");
  printf("Created\n");
  matcher = new SaveableFlannBasedMatcher(filename);
  printf("Loading matcher '%s'...\n", filename);
  matcher->load();
  printf("Loaded!\n");
}

long Recogniser::query(const char* imagepath)
{
  Mat queryImage = imread(imagepath);

  std::vector<DMatch> matches;
  std::vector<std::vector<DMatch> > knn_matches;
  matches.clear();

  //detect keypoints and compute descriptors of query image using the detector
  std::vector<KeyPoint> keypoints;
  Mat descriptors;
  detector->detectAndCompute(queryImage, noArray(), keypoints, descriptors, false);

  //KNN match the query images to the training set with N=2
  matcher->knnMatch(descriptors, knn_matches, 2);

  //Filter the matches according to a threshold
  loweFilter(knn_matches, matches);

  // Free memory
  descriptors.release();
  queryImage.release();

  return matches.size();
}

// Python Wrapper
BOOST_PYTHON_MODULE(recogniser)
{
  class_<Recogniser>("Recogniser", init<const char*>())
      .def("query", &Recogniser::query)
  ;
}
