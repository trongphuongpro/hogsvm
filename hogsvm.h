#ifndef GUARD_HOGSVM_H
#define GUARD_HOGSVM_H

#include <vector>
#include "opencv2/imgproc.hpp"
#include "opencv2/ml.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/objdetect.hpp"

class HOGSVM {
public:

	HOGSVM(int = 9, 
			cv::Size = cv::Size(8,8), 
			cv::Size = cv::Size(16,16), 
			cv::Size = cv::Size(8,8));


	std::vector<float> getDetector(const cv::Ptr<cv::ml::SVM>&);

	void loadTrainingSet(const char*, 
						const char*);

	
	void train();
	void evaluate(const char*);
	std::vector<cv::Rect> detect(const cv::Mat&, int = 8, float = 1.15);

	int testVideo(const char*);
	void saveModel(const cv::String);
	void loadModel(const cv::String);
	void showInfo();



private:

	cv::Size windowSize;

	int posCount, negCount;
	int truePos, posPredict, posActual;

	std::vector<cv::Mat> trainingImages;
	std::vector<cv::Mat> testingImages;
	std::vector<int> trainingLabels;
	std::vector<cv::Mat> gradientList;
	std::vector<cv::Mat> negImageList;
	cv::Ptr<cv::ml::SVM> svm;
	cv::HOGDescriptor hog;
	
	cv::Mat trainData;

	std::vector<cv::Mat> loadImages(const char*);
	void loadPositiveImages(const char*);
	void loadNegativeImages(const char*);

	int samplePositiveImages(const char*, bool);
	void sampleNegativeImages(const std::vector<cv::Mat>&);

	void computeHOG(bool = false);
	void chooseWindowSize(cv::Size&);
	void prepareData();
	std::vector<float> getLinearSVC();
	void softTrain(float);
	void hardNegativeMine();

	std::vector<cv::Rect> nonMaxSuppression(const std::vector<double>&, 
											const std::vector<cv::Rect>&,
											float = 0.3);

	int computeIOU(const std::vector<cv::Rect>&, const cv::Rect&);

	template<typename T>
	std::vector<std::size_t> argsort(const std::vector<T>&);
};

#endif