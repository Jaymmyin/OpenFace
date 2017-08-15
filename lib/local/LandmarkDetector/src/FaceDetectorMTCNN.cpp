///////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016, Carnegie Mellon University and University of Cambridge,
// all rights reserved.
//
// THIS SOFTWARE IS PROVIDED �AS IS� FOR ACADEMIC USE ONLY AND ANY EXPRESS
// OR IMPLIED WARRANTIES WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS
// BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY.
// OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Notwithstanding the license granted herein, Licensee acknowledges that certain components
// of the Software may be covered by so-called �open source� software licenses (�Open Source
// Components�), which means any software licenses approved as open source licenses by the
// Open Source Initiative or any substantially similar licenses, including without limitation any
// license that, as a condition of distribution of the software licensed under such license,
// requires that the distributor make the software available in source code format. Licensor shall
// provide a list of Open Source Components for a particular version of the Software upon
// Licensee�s request. Licensee will comply with the applicable terms of such licenses and to
// the extent required by the licenses covering Open Source Components, the terms of such
// licenses will apply in lieu of the terms of this Agreement. To the extent the terms of the
// licenses applicable to Open Source Components prohibit any of the restrictions in this
// License Agreement with respect to such Open Source Component, such restrictions will not
// apply to such Open Source Component. To the extent the terms of the licenses applicable to
// Open Source Components require Licensor to make an offer to provide source code or
// related information in connection with the Software, such offer is hereby made. Any request
// for source code or related information should be directed to cl-face-tracker-distribution@lists.cam.ac.uk
// Licensee acknowledges receipt of notices for the Open Source Components for the initial
// delivery of the Software.

//     * Any publications arising from the use of this software, including but
//       not limited to academic journal and conference publications, technical
//       reports and manuals, must cite at least one of the following works:
//
//       OpenFace: an open source facial behavior analysis toolkit
//       Tadas Baltru�aitis, Peter Robinson, and Louis-Philippe Morency
//       in IEEE Winter Conference on Applications of Computer Vision, 2016  
//
//       Rendering of Eyes for Eye-Shape Registration and Gaze Estimation
//       Erroll Wood, Tadas Baltru�aitis, Xucong Zhang, Yusuke Sugano, Peter Robinson, and Andreas Bulling 
//       in IEEE International. Conference on Computer Vision (ICCV),  2015 
//
//       Cross-dataset learning and person-speci?c normalisation for automatic Action Unit detection
//       Tadas Baltru�aitis, Marwa Mahmoud, and Peter Robinson 
//       in Facial Expression Recognition and Analysis Challenge, 
//       IEEE International Conference on Automatic Face and Gesture Recognition, 2015 
//
//       Constrained Local Neural Fields for robust facial landmark detection in the wild.
//       Tadas Baltru�aitis, Peter Robinson, and Louis-Philippe Morency. 
//       in IEEE Int. Conference on Computer Vision Workshops, 300 Faces in-the-Wild Challenge, 2013.    
//
///////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"

#include "FaceDetectorMTCNN.h"

// OpenCV includes
#include <opencv2/core/core.hpp>
#include <opencv2/imgproc.hpp>

// TBB includes
#include <tbb/tbb.h>

// System includes
#include <fstream>

// Math includes
#define _USE_MATH_DEFINES
#include <cmath>

// Boost includes
#include <filesystem.hpp>
#include <filesystem/fstream.hpp>


#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "LandmarkDetectorUtils.h"

using namespace LandmarkDetector;

// Copy constructor
FaceDetectorMTCNN::FaceDetectorMTCNN(const FaceDetectorMTCNN& other) : PNet(other.PNet), RNet(other.RNet), ONet(other.ONet)
{
}

CNN::CNN(const CNN& other) : cnn_layer_types(other.cnn_layer_types), cnn_max_pooling_layers(other.cnn_max_pooling_layers), cnn_convolutional_layers_bias(other.cnn_convolutional_layers_bias)
{
	this->cnn_convolutional_layers.resize(other.cnn_convolutional_layers.size());
	for (size_t l = 0; l < other.cnn_convolutional_layers.size(); ++l)
	{
		this->cnn_convolutional_layers[l].resize(other.cnn_convolutional_layers[l].size());

		for (size_t i = 0; i < other.cnn_convolutional_layers[l].size(); ++i)
		{
			this->cnn_convolutional_layers[l][i].resize(other.cnn_convolutional_layers[l][i].size());

			for (size_t k = 0; k < other.cnn_convolutional_layers[l][i].size(); ++k)
			{
				// Make sure the matrix is copied.
				this->cnn_convolutional_layers[l][i][k] = other.cnn_convolutional_layers[l][i][k].clone();
			}
		}
	}

	this->cnn_fully_connected_layers_weights.resize(other.cnn_fully_connected_layers_weights.size());

	for (size_t l = 0; l < other.cnn_fully_connected_layers_weights.size(); ++l)
	{
		// Make sure the matrix is copied.
		this->cnn_fully_connected_layers_weights[l] = other.cnn_fully_connected_layers_weights[l].clone();
	}

	this->cnn_fully_connected_layers_biases.resize(other.cnn_fully_connected_layers_biases.size());

	for (size_t l = 0; l < other.cnn_fully_connected_layers_biases.size(); ++l)
	{
		// Make sure the matrix is copied.
		this->cnn_fully_connected_layers_biases[l] = other.cnn_fully_connected_layers_biases[l].clone();
	}

	this->cnn_prelu_layer_weights.resize(other.cnn_prelu_layer_weights.size());

	for (size_t l = 0; l < other.cnn_prelu_layer_weights.size(); ++l)
	{
		// Make sure the matrix is copied.
		this->cnn_prelu_layer_weights[l] = other.cnn_prelu_layer_weights[l].clone();
	}
}

std::vector<cv::Mat_<float>> CNN::Inference(const cv::Mat& input_img)
{
	if (input_img.channels() == 1)
	{
		cv::cvtColor(input_img, input_img, cv::COLOR_GRAY2BGR);
	}

	int cnn_layer = 0;
	int fully_connected_layer = 0;
	int prelu_layer = 0;
	int max_pool_layer = 0;

	// Slit a BGR image into three chnels
	cv::Mat channels[3]; 
	cv::split(input_img, channels);  

	// Flip the BGR order to RGB
	vector<cv::Mat_<float> > input_maps;
	input_maps.push_back(channels[2]);
	input_maps.push_back(channels[1]);
	input_maps.push_back(channels[0]);

	vector<cv::Mat_<float> > outputs;

	for (size_t layer = 0; layer < cnn_layer_types.size(); ++layer)
	{

		// Determine layer type
		int layer_type = cnn_layer_types[layer];

		// Convolutional layer
		if (layer_type == 0)
		{
			outputs.clear();
			for (size_t in = 0; in < input_maps.size(); ++in)
			{
				cv::Mat_<float> input_image = input_maps[in];

				// Useful precomputed data placeholders for quick correlation (convolution)
				cv::Mat_<double> input_image_dft;
				cv::Mat integral_image;
				cv::Mat integral_image_sq;

				// TODO can TBB-ify this
				for (size_t k = 0; k < cnn_convolutional_layers[cnn_layer][in].size(); ++k)
				{
					cv::Mat_<float> kernel = cnn_convolutional_layers[cnn_layer][in][k];

					// The convolution (with precomputation)
					cv::Mat_<float> output;
					if (cnn_convolutional_layers_dft[cnn_layer][in][k].second.empty())
					{
						std::map<int, cv::Mat_<double> > precomputed_dft;

						LandmarkDetector::matchTemplate_m(input_image, input_image_dft, integral_image, integral_image_sq, kernel, precomputed_dft, output, CV_TM_CCORR);

						cnn_convolutional_layers_dft[cnn_layer][in][k].first = precomputed_dft.begin()->first;
						cnn_convolutional_layers_dft[cnn_layer][in][k].second = precomputed_dft.begin()->second;
					}
					else
					{
						std::map<int, cv::Mat_<double> > precomputed_dft;
						precomputed_dft[cnn_convolutional_layers_dft[cnn_layer][in][k].first] = cnn_convolutional_layers_dft[cnn_layer][in][k].second;
						LandmarkDetector::matchTemplate_m(input_image, input_image_dft, integral_image, integral_image_sq, kernel, precomputed_dft, output, CV_TM_CCORR);
					}

					// Combining the maps
					if (in == 0)
					{
						outputs.push_back(output);
					}
					else
					{
						outputs[k] = outputs[k] + output;
					}

				}

			}

			for (size_t k = 0; k < cnn_convolutional_layers[cnn_layer][0].size(); ++k)
			{
				outputs[k] = outputs[k] + cnn_convolutional_layers_bias[cnn_layer][k];
			}
			cnn_layer++;
		}
		if (layer_type == 1)
		{
			vector<cv::Mat_<float> > outputs_sub;

			int stride_x = std::get<2>(cnn_max_pooling_layers[max_pool_layer]);
			int stride_y = std::get<3>(cnn_max_pooling_layers[max_pool_layer]);
			
			int kernel_size_x = std::get<0>(cnn_max_pooling_layers[max_pool_layer]);
			int kernel_size_y = std::get<1>(cnn_max_pooling_layers[max_pool_layer]);

			// Iterate over kernel height and width, based on stride
			for (size_t in = 0; in < input_maps.size(); ++in)
			{
				// Help with rounding up a bit, to match caffe style output
				int out_x = round((double)(input_maps[in].cols - kernel_size_x) / (double)stride_x) + 1;
				int out_y = round((double)(input_maps[in].rows - kernel_size_y) / (double)stride_y) + 1;

				cv::Mat_<float> sub_out(out_y, out_x, 0.0);
				cv::Mat_<float> in_map = input_maps[in];
				
				for (int x = 0; x < input_maps[in].cols; x += stride_x)
				{
					int max_x = cv::min(input_maps[in].cols, x + kernel_size_x);
					int x_in_out = floor(x / stride_x);

					if (x_in_out >= out_x)
						continue;

					for (int y = 0; y < input_maps[in].rows; y += stride_y)
					{
						int y_in_out = floor(y / stride_y);

						if (y_in_out >= out_y)
							continue;

						int max_y = cv::min(input_maps[in].rows, y + kernel_size_y);

						float curr_max = -FLT_MAX;
						
						for (int x_in = x; x_in < max_x; ++x_in)
						{
							for (int y_in = y; y_in < max_y; ++y_in)
							{
								float curr_val = in_map.at<float>(y_in, x_in);
								if (curr_val > curr_max)
								{
									curr_max = curr_val;
								}
							}
						}
						sub_out.at<float>(y_in_out, x_in_out) = curr_max;
					}
				}

				outputs_sub.push_back(sub_out);

			}
			outputs = outputs_sub;
		}
		if (layer_type == 2)
		{

			if(input_maps.size() > 1)
			{
				// Concatenate all the maps
				cv::Size orig_size = input_maps[0].size();
				cv::Mat_<float> input_concat = input_maps[0].t();
				input_concat = input_concat.reshape(0, 1);

				for (size_t in = 1; in < input_maps.size(); ++in)
				{
					cv::Mat_<float> add = input_maps[in].t();
					add = add.reshape(0, 1);
					cv::vconcat(input_concat, add, input_concat);
				}

				input_concat = input_concat.t() * cnn_fully_connected_layers_weights[fully_connected_layer];

				// Add biases
				for (size_t k = 0; k < cnn_fully_connected_layers_biases[fully_connected_layer].rows; ++k)
				{
					input_concat.col(k) = input_concat.col(k) + cnn_fully_connected_layers_biases[fully_connected_layer].at<float>(k);
				}

				outputs.clear();
				// Resize and add as output
				for (size_t k = 0; k < cnn_fully_connected_layers_biases[fully_connected_layer].rows; ++k)
				{
					cv::Mat_<float> reshaped = input_concat.col(k).clone();
					reshaped = reshaped.reshape(1, orig_size.width).t();
					outputs.push_back(reshaped);
				}
			}
			else
			{
				cv::Mat out = input_maps[0].t() * cnn_fully_connected_layers_weights[fully_connected_layer] + cnn_fully_connected_layers_biases[fully_connected_layer].t();
				outputs.clear();
				outputs.push_back(out);
			}

			fully_connected_layer++;
		}
		if (layer_type == 3) // PReLU
		{
			outputs.clear();
			for (size_t k = 0; k < input_maps.size(); ++k)
			{
				// Apply the PReLU
				cv::Mat_<float> pos;
				cv::threshold(input_maps[k], pos, 0, 0, cv::THRESH_TOZERO);
				cv::Mat_<float> neg;
				cv::threshold(input_maps[k], neg, 0, 0, cv::THRESH_TOZERO_INV);
				outputs.push_back(pos + neg * cnn_prelu_layer_weights[prelu_layer].at<float>(k));

			}
			prelu_layer++;
		}
		if (layer_type == 4)
		{
			outputs.clear();
			for (size_t k = 0; k < input_maps.size(); ++k)
			{
				// Apply the sigmoid
				cv::exp(-input_maps[k], input_maps[k]);
				input_maps[k] = 1.0 / (1.0 + input_maps[k]);

				outputs.push_back(input_maps[k]);

			}
		}
		// Set the outputs of this layer to inputs of the next
		input_maps = outputs;
		
	}

	
	return outputs;

}

void ReadMatBin(std::ifstream& stream, cv::Mat &output_mat)
{
	// Read in the number of rows, columns and the data type
	int row, col, type;

	stream.read((char*)&row, 4);
	stream.read((char*)&col, 4);
	stream.read((char*)&type, 4);

	output_mat = cv::Mat(row, col, type);
	int size = output_mat.rows * output_mat.cols * output_mat.elemSize();
	stream.read((char *)output_mat.data, size);

}

void CNN::Read(string location)
{
	ifstream cnn_stream(location, ios::in | ios::binary);
	if (cnn_stream.is_open())
	{
		cnn_stream.seekg(0, ios::beg);

		// Reading in CNNs

		int network_depth;
		cnn_stream.read((char*)&network_depth, 4);

		cnn_layer_types.resize(network_depth);

		for (int layer = 0; layer < network_depth; ++layer)
		{

			int layer_type;
			cnn_stream.read((char*)&layer_type, 4);
			cnn_layer_types[layer] = layer_type;

			// convolutional
			if (layer_type == 0)
			{

				// Read the number of input maps
				int num_in_maps;
				cnn_stream.read((char*)&num_in_maps, 4);

				// Read the number of kernels for each input map
				int num_kernels;
				cnn_stream.read((char*)&num_kernels, 4);

				vector<vector<cv::Mat_<float> > > kernels;
				vector<vector<pair<int, cv::Mat_<double> > > > kernel_dfts;

				kernels.resize(num_in_maps);
				kernel_dfts.resize(num_in_maps);

				vector<float> biases;
				for (int k = 0; k < num_kernels; ++k)
				{
					float bias;
					cnn_stream.read((char*)&bias, 4);
					biases.push_back(bias);
				}

				cnn_convolutional_layers_bias.push_back(biases);

				// For every input map
				for (int in = 0; in < num_in_maps; ++in)
				{
					kernels[in].resize(num_kernels);
					kernel_dfts[in].resize(num_kernels);

					// For every kernel on that input map
					for (int k = 0; k < num_kernels; ++k)
					{
						ReadMatBin(cnn_stream, kernels[in][k]);

					}
				}

				cnn_convolutional_layers.push_back(kernels);
				cnn_convolutional_layers_dft.push_back(kernel_dfts);
			}
			else if (layer_type == 1)
			{
				int kernel_x, kernel_y, stride_x, stride_y;
				cnn_stream.read((char*)&kernel_x, 4);
				cnn_stream.read((char*)&kernel_y, 4);
				cnn_stream.read((char*)&stride_x, 4);
				cnn_stream.read((char*)&stride_y, 4);
				cnn_max_pooling_layers.push_back(std::tuple<int, int, int, int>(kernel_x, kernel_y, stride_x, stride_y));
			}
			else if (layer_type == 2)
			{
				cv::Mat_<float> biases;
				ReadMatBin(cnn_stream, biases);
				cnn_fully_connected_layers_biases.push_back(biases);

				// Fully connected layer
				cv::Mat_<float> weights;
				ReadMatBin(cnn_stream, weights);
				cnn_fully_connected_layers_weights.push_back(weights);
			}

			else if (layer_type == 3)
			{
				cv::Mat_<float> weights;
				ReadMatBin(cnn_stream, weights);
				cnn_prelu_layer_weights.push_back(weights);
			}
		}
	}
	else
	{
		cout << "WARNING: Can't find the CNN location" << endl;
	}
}

//===========================================================================
// Read in the MTCNN detector
void FaceDetectorMTCNN::Read(string location)
{

	cout << "Reading the MTCNN face detector from: " << location << endl;

	ifstream locations(location.c_str(), ios_base::in);
	if (!locations.is_open())
	{
		cout << "Couldn't open the model file, aborting" << endl;
		return;
	}
	string line;

	// The other module locations should be defined as relative paths from the main model
	boost::filesystem::path root = boost::filesystem::path(location).parent_path();

	// The main file contains the references to other files
	while (!locations.eof())
	{
		getline(locations, line);

		stringstream lineStream(line);

		string module;
		string location;

		// figure out which module is to be read from which file
		lineStream >> module;

		lineStream >> location;

		// remove carriage return at the end for compatibility with unix systems
		if (location.size() > 0 && location.at(location.size() - 1) == '\r')
		{
			location = location.substr(0, location.size() - 1);
		}

		// append to root
		location = (root / location).string();
		if (module.compare("PNet") == 0)
		{
			cout << "Reading the PNet module from: " << location << endl;
			PNet.Read(location);
		}
		else if(module.compare("RNet") == 0)
		{
			cout << "Reading the RNet module from: " << location << endl;
			RNet.Read(location);
		}
		else if (module.compare("ONet") == 0)
		{
			cout << "Reading the ONet module from: " << location << endl;
			ONet.Read(location);
		}
	}
}

// Perform non maximum supression on proposal bounding boxes prioritizing boxes with high score/confidence
std::vector<int> non_maximum_supression(const std::vector<cv::Rect_<float> >& original_bb, const std::vector<float>& scores, float thresh)
{

	// Sort the input bounding boxes by the detection score, using the nice trick of multimap always being sorted internally
	std::multimap<float, size_t> idxs;
	for (size_t i = 0; i < original_bb.size(); ++i)
	{
		idxs.insert(std::pair<float, size_t>(scores[i], i));
	}

	std::vector<int> output_ids;

	// keep looping while some indexes still remain in the indexes list
	while (idxs.size() > 0)
	{
		// grab the last rectangle
		auto lastElem = --std::end(idxs);
		size_t curr_id = lastElem->second;

		const cv::Rect& rect1 = original_bb[curr_id];

		idxs.erase(lastElem);

		// Iterate through remaining bounding boxes and choose which ones to remove
		for (auto pos = std::begin(idxs); pos != std::end(idxs); )
		{
			// grab the current rectangle
			const cv::Rect& rect2 = original_bb[pos->second];

			float intArea = (rect1 & rect2).area();
			float unionArea = rect1.area() + rect2.area() - intArea;
			float overlap = intArea / unionArea;

			// Remove the bounding boxes with less confidence but with significant overlap with the current one
			if (overlap > thresh)
			{
				pos = idxs.erase(pos);
			}
			else
			{
				++pos;
			}
		}
		output_ids.push_back(curr_id);

	}

	return output_ids;

}

// Helper function for selecting a subset of bounding boxes based on indices
void select_subset(const vector<int>& to_keep, vector<cv::Rect_<float> >& bounding_boxes, vector<float>& scores, vector<cv::Rect_<float> >& corrections)
{
	vector<cv::Rect_<float> > bounding_boxes_tmp;
	vector<float> scores_tmp;
	vector<cv::Rect_<float> > corrections_tmp;

	for (size_t i = 0; i < to_keep.size(); ++i)
	{
		bounding_boxes_tmp.push_back(bounding_boxes[to_keep[i]]);
		scores_tmp.push_back(scores[to_keep[i]]);
		corrections_tmp.push_back(corrections[to_keep[i]]);
	}

	bounding_boxes = bounding_boxes_tmp;
	scores = scores_tmp;
	corrections = corrections_tmp;
}

// Use the heatmap generated by PNet to generate bounding boxes in the original image space, also generate the correction values and scores of the bounding boxes as well
void generate_bounding_boxes(vector<cv::Rect_<float> >& o_bounding_boxes, vector<float>& o_scores, vector<cv::Rect_<float> >& o_corrections, const cv::Mat_<float>& heatmap, const vector<cv::Mat_<float> >& corrections, double scale, double threshold, int face_support)
{

	// Correction for the pooling
	int stride = 2;

	o_bounding_boxes.clear();
	o_scores.clear();
	o_corrections.clear();

	int counter = 0;
	for (int x = 0; x < heatmap.cols; ++x)
	{
		for(int y = 0; y < heatmap.rows; ++y)
		{
			if (heatmap.at<float>(y, x) >= threshold)
			{
				float min_x = int((stride * x + 1) / scale);
				float max_x = int((stride * x + face_support) / scale);
				float min_y = int((stride * y + 1) / scale);
				float max_y = int((stride * y + face_support) / scale);

				o_bounding_boxes.push_back(cv::Rect_<float>(min_x, min_y, max_x - min_x, max_y - min_y));
				o_scores.push_back(heatmap.at<float>(y, x));

				float corr_x = corrections[0].at<float>(y, x);
				float corr_y = corrections[1].at<float>(y, x);
				float corr_width = corrections[2].at<float>(y, x);
				float corr_height = corrections[3].at<float>(y, x);
				o_corrections.push_back(cv::Rect_<float>(corr_x, corr_y, corr_width, corr_height));

				counter++;
			}
		}
	}
	
}

// Converting the bounding boxes to squares
void rectify(vector<cv::Rect_<float> >& total_bboxes)
{

	// Apply size and location offsets
	for (size_t i = 0; i < total_bboxes.size(); ++i)
	{
		float height = total_bboxes[i].height;
		float width = total_bboxes[i].width;

		float max_side = max(width, height);

		// Correct the starts based on new size
		float new_min_x = total_bboxes[i].x + 0.5 * (width - max_side);
		float new_min_y = total_bboxes[i].y + 0.5 * (height - max_side);

		total_bboxes[i].x = (int)new_min_x;
		total_bboxes[i].y = (int)new_min_y;
		total_bboxes[i].width = (int)max_side;
		total_bboxes[i].height = (int)max_side;
	}
}

void apply_correction(vector<cv::Rect_<float> >& total_bboxes, const vector<cv::Rect_<float> > corrections, bool add1)
{

	// Apply size and location offsets
	for (size_t i = 0; i < total_bboxes.size(); ++i)
	{
		cv::Rect curr_box = total_bboxes[i];
		if (add1)
		{
			curr_box.width++;
			curr_box.height++;
		}

		float new_min_x = curr_box.x + corrections[i].x * curr_box.width;
		float new_min_y = curr_box.y + corrections[i].y * curr_box.height;
		float new_max_x = curr_box.x + curr_box.width + curr_box.width * corrections[i].width;
		float new_max_y = curr_box.y + curr_box.height + curr_box.height * corrections[i].height;
		total_bboxes[i] = cv::Rect_<float>(new_min_x, new_min_y, new_max_x - new_min_x, new_max_y - new_min_y);

	}


}


// The actual MTCNN face detection step
bool FaceDetectorMTCNN::DetectFaces(vector<cv::Rect_<double> >& o_regions, const cv::Mat& input_img, std::vector<double>& o_confidences, int min_face_size, double t1, double t2, double t3)
{

	int height_orig = input_img.size().height;
	int width_orig = input_img.size().width;

	// Size ratio of image pyramids
	double pyramid_factor = 0.709;

	// Face support region is 12x12 px, so from that can work out the largest
	// scale(which is 12 / min), and work down from there to smallest scale(no smaller than 12x12px)
	int min_dim = std::min(height_orig, width_orig);

	int face_support = 12;
	int num_scales = floor(log((double)min_face_size / (double)min_dim) / log(pyramid_factor)) + 1;

	if (input_img.channels() == 1)
	{
		cv::cvtColor(input_img, input_img, CV_GRAY2RGB);
	}

	cv::Mat img_float;
	input_img.convertTo(img_float, CV_32FC3);

	vector<cv::Rect_<float> > proposal_boxes_all;
	vector<float> scores_all;
	vector<cv::Rect_<float> > proposal_corrections_all;

	for (int i = 0; i < num_scales; ++i)
	{
		double scale = ((double)face_support / (double)min_face_size)*cv::pow(pyramid_factor, i);

		int h_pyr = ceil(height_orig * scale);
		int w_pyr = ceil(width_orig * scale);

		cv::Mat normalised_img;
		cv::resize(img_float, normalised_img, cv::Size(w_pyr, h_pyr));
		
		// Normalize the image
		normalised_img = (normalised_img - 127.5) * 0.0078125;

		// Actual PNet CNN step
		std::vector<cv::Mat_<float> > pnet_out = PNet.Inference(normalised_img);
		
		// Extract the probabilities from PNet response
		cv::Mat_<float> prob_heatmap;
		cv::exp(pnet_out[0]- pnet_out[1], prob_heatmap);
		prob_heatmap = 1.0 / (1.0 + prob_heatmap);

		// Extract the probabilities from PNet response
		std::vector<cv::Mat_<float>> corrections_heatmap(pnet_out.begin() + 2, pnet_out.end());

		// Grab the detections
		vector<cv::Rect_<float> > proposal_boxes;
		vector<float> scores;
		vector<cv::Rect_<float> > proposal_corrections;
		generate_bounding_boxes(proposal_boxes, scores, proposal_corrections, prob_heatmap, corrections_heatmap, scale, t1, face_support);

		// Perform non-maximum supression on proposals in this scale
		vector<int> to_keep = non_maximum_supression(proposal_boxes, scores, 0.5);
		select_subset(to_keep, proposal_boxes, scores, proposal_corrections);

		proposal_boxes_all.insert(proposal_boxes_all.end(), proposal_boxes.begin(), proposal_boxes.end());
		scores_all.insert(scores_all.end(), scores.begin(), scores.end());
		proposal_corrections_all.insert(proposal_corrections_all.end(), proposal_corrections.begin(), proposal_corrections.end());
		
	}

	// Preparation for RNet step

	// Non maximum supression accross bounding boxes, and their offset correction
	vector<int> to_keep = non_maximum_supression(proposal_boxes_all, scores_all, 0.7);
	select_subset(to_keep, proposal_boxes_all, scores_all, proposal_corrections_all);

	apply_correction(proposal_boxes_all, proposal_corrections_all, false);

	// Convert to rectangles and round
	rectify(proposal_boxes_all);

	// Creating proposal images from previous step detections
	vector<cv::Mat> proposal_imgs;
	for (size_t k = 0; k < proposal_boxes_all.size(); ++k)
	{
		float width_target = proposal_boxes_all[k].width + 1;
		float height_target = proposal_boxes_all[k].height + 1;

		// Work out the start and end indices in the original image
		int start_x_in = cv::max((int)(proposal_boxes_all[k].x - 1), 0);
		int start_y_in = cv::max((int)(proposal_boxes_all[k].y - 1), 0);
		int end_x_in = cv::min((int)(proposal_boxes_all[k].x + width_target - 1), width_orig);
		int end_y_in = cv::min((int)(proposal_boxes_all[k].y + height_target - 1), height_orig);

		// Work out the start and end indices in the target image
		int	start_x_out = cv::max((int)(-proposal_boxes_all[k].x + 1), 0);
		int start_y_out = cv::max((int)(-proposal_boxes_all[k].y + 1), 0);
		int end_x_out = cv::min(width_target - (proposal_boxes_all[k].x + proposal_boxes_all[k].width - width_orig), width_target);
		int end_y_out = cv::min(height_target - (proposal_boxes_all[k].y + proposal_boxes_all[k].height - height_orig), height_target);

		cv::Mat tmp(height_target, width_target, CV_32FC3);

		img_float(cv::Rect(start_x_in, start_y_in, end_x_in - start_x_in, end_y_in - start_y_in)).copyTo(
			tmp(cv::Rect(start_x_out, start_y_out, end_x_out - start_x_out, end_y_out - start_y_out)));
		
		cv::Mat prop_img;
		cv::resize(tmp, prop_img, cv::Size(24, 24));
			
		prop_img = (prop_img - 127.5) * 0.0078125;
		
		proposal_imgs.push_back(prop_img);

		// Perform RNet on the proposal image
		std::vector<cv::Mat_<float> > rnet_out = RNet.Inference(prop_img);
	}

	return true;

}
