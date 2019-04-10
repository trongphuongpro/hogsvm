#include "hogsvm.h"
#include "opencv2/core/utility.hpp"
#include "tinyxml2.h"
#include <iostream>
#include <algorithm>
#include <iterator>
#include <ctime>
#include <iomanip>


using namespace cv;
using namespace cv::ml;
using namespace std;
using namespace tinyxml2;


HOGSVM::HOGSVM(int _bin, 
				Size _cellSize, 
				Size _blockSize, 
				Size _blockStride) {

	hog.nbins = _bin;
	hog.cellSize = _cellSize;
	hog.blockSize = _blockSize;
	hog.blockStride = _blockStride;

	posCount = 0;
	negCount = 0;

	truePos = posPredict = posActual = 0;
}


void HOGSVM::loadTrainingSet(const char* annotation,
							const char* neg) {

	loadPositiveImages(annotation);
	loadNegativeImages(neg);
	
}


void HOGSVM::loadNegativeImages(const char* path) {

	if (path != NULL) {
		negImageList = loadImages(path);
		sampleNegativeImages(negImageList);

		negCount = negImageList.size();

		trainingLabels.insert(trainingLabels.end(), negCount, -1);
	}
}


vector<Mat> HOGSVM::loadImages(const char* dirname) {

	Mat img;
	vector<Mat> imgList;
	vector<String> files;

	glob(dirname, files, true);

	for (size_t i = 0; i < files.size(); i++) {
		img = imread(files[i]);
		if (img.empty()) {
			cerr << files[i] << " is invalid!" << endl;
			continue;
		}

		imgList.push_back(img);
	}

	clog << "Negative set size: " << imgList.size() << endl;

	return imgList;
}


void HOGSVM::loadPositiveImages(const char* annotation) {

	int counter = samplePositiveImages(annotation, true);

    windowSize.width /= counter;
    windowSize.height /= counter;

    clog << "avg. window: " << windowSize << endl;

	chooseWindowSize(windowSize);

	clog << "window: " << windowSize << endl;
	clog << "Positive set size: " << counter << endl;

	posCount += counter;
	trainingLabels.insert(trainingLabels.end(), counter, +1);
}


int HOGSVM::samplePositiveImages(const char* annotation, bool sampling) {
	string path(annotation);
	path = path.substr(0, path.find_last_of("/")) + '/';

	XMLDocument xmlDoc;
    XMLError eResult = xmlDoc.LoadFile(annotation);
 
    if (eResult) {
        cerr << XMLDocument::ErrorIDToName(eResult) << endl;
        exit(1);
    }
 
    XMLElement *root = xmlDoc.RootElement();
 
    XMLElement *image = root->FirstChildElement("images")
  						    ->FirstChildElement("image");
    const char *filename;
 	Mat img;
 	int counter = 0;
    while (image != nullptr) {
    	filename = NULL;
        XMLError eResult = image->QueryStringAttribute("file", &filename);

        if (eResult) {
        	cerr << XMLDocument::ErrorIDToName(eResult) << endl;
        	continue;
        }


 		img = imread(path + filename);

        Rect bb;
        int ignore;

        XMLElement *box = image->FirstChildElement("box");
        while (box != nullptr) {
        	ignore = 0;
        	box->QueryAttribute("ignore", &ignore);

        	if (!ignore) {
        		counter++;
        	
            	box->QueryAttribute("top", &(bb.y));
            	box->QueryAttribute("left", &(bb.x));
            	box->QueryAttribute("width", &(bb.width));
            	box->QueryAttribute("height", &(bb.height));
 
 				if (sampling) {
 					Mat roi = img(bb);

            		windowSize.width += bb.width;
            		windowSize.height += bb.height;

            		trainingImages.push_back(roi.clone());
            	}
            	else {
            		vector<Rect> detections = detect(img, 8, 1.15);
            		posPredict += detections.size();
            		truePos += computeIOU(detections, bb);
            	}
        	}
        	
            box = box->NextSiblingElement();
        }
        image = image->NextSiblingElement();
    }
    return counter;
}


void HOGSVM::chooseWindowSize(Size& windowSize) {
	
	float origRatio = float(windowSize.width) / windowSize.height;
	Size smallSize(windowSize.width / 16 * 8, windowSize.height / 16 * 8);
	Size bigSize((windowSize.width/16 + 1) * 8, (windowSize.height/16 + 1) * 8);
	float smallSizeRatio = float(smallSize.width) / smallSize.height;
	float bigSizeRatio = float(bigSize.width) / bigSize.height;

	if (abs(origRatio-smallSizeRatio) > abs(origRatio-bigSizeRatio)) {
		windowSize = bigSize;
	}
	else {
		windowSize = smallSize; 
	}

	hog.winSize = windowSize;
}


void HOGSVM::sampleNegativeImages(const vector<Mat>& full_neg_lst) {

	Rect box;
	box.width = windowSize.width;
	box.height = windowSize.height;

	const int size_x = box.width;
	const int size_y = box.height;

	srand((unsigned int)time(NULL));

	for (size_t i = 0; i < full_neg_lst.size(); i++) {
		if (full_neg_lst[i].cols > box.width
			&& full_neg_lst[i].rows > box.height) {

			box.x = rand() % (full_neg_lst[i].cols - size_x);
			box.y = rand() % (full_neg_lst[i].rows - size_y);

			Mat roi = full_neg_lst[i](box);
			trainingImages.push_back(roi.clone());
		}
	}
}


void HOGSVM::computeHOG(bool use_flip) {

	Mat gray;
	vector<float> descriptors;
	gradientList.clear();

	for (size_t i = 0; i < trainingImages.size(); i++) {

		resize(trainingImages[i], trainingImages[i], windowSize);
		cvtColor(trainingImages[i], gray, COLOR_BGR2GRAY);

		hog.compute(gray, descriptors, Size(8,8), Size(0,0));
		gradientList.push_back(Mat(descriptors).clone());

		if (use_flip) {
			flip(gray, gray, 1);
			hog.compute(gray, descriptors, Size(8,8), Size(0,0));
			gradientList.push_back(Mat(descriptors).clone());
		}
	}
}


void HOGSVM::train() {
	if (posCount > 0 && negCount > 0) {
		computeHOG();
		prepareData();

		softTrain(1.0);
		hardNegativeMine();

		computeHOG();
		prepareData();

		clog << "Training SVM...";
		svm->train(trainData, ROW_SAMPLE, trainingLabels);
		clog << "...[Done]" << endl;

		hog.setSVMDetector(getLinearSVC());
		cout << "C: " << svm->getC() << " Nu: " << svm->getNu() << endl;
	}
	else {
		cerr << "No training data!" << endl;
	}
}


void HOGSVM::softTrain(float C) {
	clog << "Training SVM...";

	svm = SVM::create();
    svm->setTermCriteria(TermCriteria(TermCriteria::MAX_ITER + TermCriteria::EPS, 
    								1000, 
    								1e-3));

	svm->setKernel(SVM::LINEAR);

    svm->setNu(0.5);
    svm->setC(C);

    svm->setType(SVM::NU_SVR);

	svm->train(trainData, ROW_SAMPLE, trainingLabels);
    hog.setSVMDetector(getLinearSVC());
    clog << "...[Done]" << endl;
}



void HOGSVM::hardNegativeMine() {
	clog << "Testing trained detector on negative images...";

	vector<Rect> detections;
	vector<double> foundWeights;
	int counter = 0;

	for (size_t i = 0; i < negImageList.size(); i++) {
		if (negImageList[i].cols >= windowSize.width
			&& negImageList[i].rows >= windowSize.height) {

			hog.detectMultiScale(negImageList[i], detections, foundWeights);
		}
		else {
			detections.clear();
		}
		for (size_t j = 0; j < detections.size(); j++) {
			counter++;

			Mat detection = negImageList[i](detections[j]).clone();
			resize(detection, detection, windowSize, 0, 0, cv::INTER_CUBIC);

			trainingImages.push_back(detection);
		}
	}

	negCount += counter;
	trainingLabels.insert(trainingLabels.end(), counter, -1);

	clog << "...[Done]" << endl;
}


void HOGSVM::prepareData() {

	const int rows = (int)gradientList.size();
	const int cols = (int)gradientList[0].rows;

	Mat tmp(1, cols, CV_32FC1);
	trainData = Mat(rows, cols, CV_32FC1);

	for(size_t i = 0; i < gradientList.size(); i++) {
		CV_Assert(gradientList[i].cols == 1);

		transpose(gradientList[i], tmp);
		tmp.copyTo(trainData.row(int(i)));
	}
}


vector<float> HOGSVM::getLinearSVC() {
	Mat sv = svm->getSupportVectors();
	const int sv_total = sv.rows;

	Mat alpha, svidx;
	double rho = svm->getDecisionFunction(0, alpha, svidx);

	CV_Assert(alpha.total() == 1 && svidx.total() == 1 && sv_total == 1);
    CV_Assert((alpha.type() == CV_64F && alpha.at<double>(0) == 1.) 
    		  || (alpha.type() == CV_32F && alpha.at<float>(0) == 1.f));
    CV_Assert(sv.type() == CV_32F);

    vector<float> hog_detector(sv.cols + 1);
    memcpy(&hog_detector[0], sv.ptr(), sv.cols*sizeof(hog_detector[0]));
    hog_detector[sv.cols] = float(-rho);

    return hog_detector;
}


void HOGSVM::saveModel(const String path) {

    hog.save(path);

}


void HOGSVM::loadModel(const String path) {

	hog.load(path);

}


int HOGSVM::testVideo(const char* filename) {

	clog << "Testing trained detector..." << endl;
	
	VideoCapture cap;
	cap.open(filename);
    Mat img;
    size_t i;
    for(i = 0;; i++) {
        if (cap.isOpened()) {
            cap >> img;
        }

        if (img.empty()) {
            break;
        }

        resize(img, img, Size(640,480));
        vector<Rect> detections;

        if (i % 3 == 0) {
        	detections = detect(img, 8, 1.15);

        	for (size_t j = 0; j < detections.size(); j++) {
        		Scalar color = Scalar(0, 255, 0);
            	rectangle(img, detections[j], color, 2);
        	}
    	}
		
        imshow("frame", img);
        if(waitKey(1) == 27) {
            break;
        }
    }
    return i;
}


void HOGSVM::showInfo() {
	clog << "Pos size: " << posCount << endl;
	clog << "Neg size: " << negCount << endl;
}


vector<Rect> HOGSVM::detect(const Mat& image, int step, float scale) {
	vector<Rect> detections;
    vector<double> foundWeights;

    Mat gray;
    cvtColor(image, gray, COLOR_BGR2GRAY);

    hog.detectMultiScale(gray, 
						detections, 
						foundWeights, 
						0, 
						Size(step,step),
						Size(0,0),
						scale);

    for (const auto& e: foundWeights)
    	cout << e << endl;

    detections = nonMaxSuppression(foundWeights, detections);

    return detections;
}


template<typename T>
	vector<size_t> HOGSVM::argsort(const vector<T>& input) {
	vector<size_t> idxs(input.size());
	iota(idxs.begin(), idxs.end(), 0);
	    
	sort(idxs.begin(), 
	  		idxs.end(), 
	  		[&](size_t a, size_t b) {
	            return input[a] < input[b];
	        });
	                
	return idxs;
}


vector<Rect> HOGSVM::nonMaxSuppression(const vector<double>& confidences, 
										const vector<Rect>& boxes,
										float overlapThresh) {

	Mat x1, y1, x2, y2;
	vector<Rect> pick;
	
	for (size_t i = 0; i < boxes.size(); i++) {
		x1.push_back(boxes[i].x);
		y1.push_back(boxes[i].y);
		x2.push_back(boxes[i].x + boxes[i].width);
		y2.push_back(boxes[i].y + boxes[i].height);
	}

	Mat area = (x2 - x1 + 1).mul(y2 - y1 + 1);
	vector<size_t> idxs = argsort(confidences);

	size_t last;

	vector<size_t>::iterator begin = idxs.begin();
	vector<size_t>::iterator end = idxs.end();

	while (begin < end) {

		vector<size_t> idxsSample(begin, end);

		last = end - begin - 1;
		size_t id = idxs[last];
		pick.push_back(boxes[id]);

		vector<size_t> suppress;
		suppress.push_back(last);

		for (size_t k = 0; k < last; k++) {
			size_t j = idxs[k];

			int xx1 = max(x1.at<int>(id), x1.at<int>(j));
			int yy1 = max(y1.at<int>(id), y1.at<int>(j));
			int xx2 = min(x2.at<int>(id), x2.at<int>(j));
			int yy2 = min(y2.at<int>(id), y2.at<int>(j));

			int w = max(0, xx2 - xx1 + 1);
			int h = max(0, yy2 - yy1 + 1);

			double overlap = float(w * h) / area.at<int>(j);

			if (overlap > overlapThresh) {
				suppress.push_back(k);
			}
		}
		for (const auto& e : suppress) {
			end = remove(begin, end, idxsSample[e]);
		}
	}
	return pick;
}


void HOGSVM::evaluate(const char* annotation) {
	posActual = samplePositiveImages(annotation, false);

	float precision = float(truePos) / posPredict;
	float recall = float(truePos) / posActual;
	float fscore = 2*precision*recall / (precision + recall);

	cout << "Precision: " << setprecision(5) << precision << endl;
	cout << "Recall: " << setprecision(5) << recall << endl;
	cout << "F1 score: " << setprecision(5) << fscore << endl;
}


int HOGSVM::computeIOU(const vector<Rect>& detections, const Rect& bb) {
	int overlapArea;
	int unionArea;
	double IoU = 0;

	for (const auto& r : detections) {
		overlapArea = (r & bb).area();
		unionArea = r.area() + bb.area() - overlapArea;

		double temp = float(overlapArea) / unionArea;
		if (IoU < temp) {
			IoU = temp;
		}
	}

	return (IoU > 0.5) ? 1 : 0;
}