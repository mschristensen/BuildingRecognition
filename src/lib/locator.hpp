#include <opencv2/opencv.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/xfeatures2d.hpp>
#include <opencv2/features2d.hpp>
#include "surf.hpp"

#include <boost/python.hpp>

using namespace cv;
using namespace boost::python;


class Locator
{
public:
  Locator();

  void locate(const char* data_filename);

  double getLat();
  double getLng();

protected:
  double lat;
  double lng;

};
