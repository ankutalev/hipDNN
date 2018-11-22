#ifndef TEST_LRN_BACKWARD_HPP
#define TEST_LRN_BACKWARD_HPP

#include "hipdnn.h"
#include "hipdnn_test_common.h"
#include "gtest/gtest.h"

Desc calculate_Dims(Desc inputDesc, Desc filterDesc, int pad[2],
                               int stride[2]) {
  assert(inputDesc.C == filterDesc.C);
  int outputHeight =
      ((inputDesc.H - filterDesc.H + 2 * pad[0]) / stride[0]) + 1;
  int outputWidth = ((inputDesc.W - filterDesc.W + 2 * pad[1]) / stride[1]) + 1;
  Desc outputDesc(inputDesc.N, filterDesc.N, outputHeight, outputWidth);
  return outputDesc;
}

struct convulution_sizes {
  convulution_sizes(int mb, int ng, int ic, int ih, int iw, int oc,
                           int oh, int ow, int kh, int kw, int padh, int padw,
                           int strh, int strw, int dilh = 0, int dilw = 0)
      : mb(mb), ng(ng), ic(ic), ih(ih), iw(iw), oc(oc), oh(oh), ow(ow), kh(kh),
        kw(kw), padh(padh), padw(padw), strh(strh), strw(strw), dilh(dilh),
        dilw(dilw) {}
  int mb;         // mini batches
  int ng;         // number of groups
  int ic, ih, iw; // Input channels, height and width
  int oc, oh, ow; // Output channels, height and width
  int kh, kw;     // kernel height and width
  int padh, padw; // padding along height and width
  int strh, strw; // stride along height and width
  int dilh, dilw; // dilation along height and width
};

template <typename dataType>
void compute_hipdnn_LRN_backward(convulution_sizes &d, dataType *src, dataType *weights,
                                dataType *dst, float *avg_time) {

	hipdnnHandle_t hipdnn;
	checkHIPDNN(hipdnnCreate(&hipdnn));
	
	hipdnnTensorDescriptor_t in_desc;
    checkHIPDNN(hipdnnCreateTensorDescriptor(&in_desc));
    checkHIPDNN(hipdnnSetTensor4dDescriptor(
        in_desc, HIPDNN_TENSOR_NCHW, HIPDNN_DATA_FLOAT,
        d.mb, d.ic, d.ih, d.iw));
		
	hipdnnFilterDescriptor_t filt_desc;
  checkHIPDNN(hipdnnCreateFilterDescriptor(&filt_desc));
  int filterDimA[] = {d.oc, d.ic, d.kh, d.kw};
  checkHIPDNN(hipdnnSetFilterNdDescriptor(filt_desc, HIPDNN_DATA_FLOAT,
                                          HIPDNN_TENSOR_NCHW, 4, filterDimA));
		
	hipdnnConvolutionDescriptor_t conv_desc;
    checkHIPDNN(hipdnnCreateConvolutionDescriptor(&conv_desc));
    checkHIPDNN(hipdnnSetConvolution2dDescriptor(
        conv_desc,
        d.padh, d.padw, d.strh, d.strw, d.dilh, d.dilw,
        HIPDNN_CROSS_CORRELATION, HIPDNN_DATA_FLOAT));
		
	checkHIPDNN(hipdnnGetConvolution2dForwardOutputDim(
        conv_desc, in_desc, filt_desc,
         &d.mb, &d.oc, &d.oh, &d.ow));
		
  hipdnnTensorDescriptor_t out_desc;
  checkHIPDNN(hipdnnCreateTensorDescriptor(&out_desc));
  checkHIPDNN(hipdnnSetTensor4dDescriptor(
        out_desc, HIPDNN_TENSOR_NCHW, HIPDNN_DATA_FLOAT,
        d.mb, d.oc, d.oh, d.ow));
		
	hipdnnConvolutionFwdAlgo_t algo; 
	
	int MaxAlgoCount =1;
    size_t ws_size{0};
    float *ws_data{nullptr};
    int calgo;
    hipdnnConvolutionFwdAlgoPerf_t algoPerf[MaxAlgoCount];        

  hipdnnFindConvolutionForwardAlgorithmEx(hipdnn, in_desc, src, filt_desc, weights, conv_desc, out_desc, dst,
          MaxAlgoCount , &calgo, algoPerf, ws_data, ws_size);
  algo = (hipdnnConvolutionFwdAlgo_t)algoPerf[0].algo;

  checkHIPDNN(hipdnnGetConvolutionForwardWorkspaceSize(hipdnn, in_desc, filt_desc, conv_desc, out_desc, algo, &ws_size));

 hipMalloc(&ws_data, ws_size);
  
  float alpha = 1.f;
  float beta = 0.f;

 checkHIPDNN(hipdnnConvolutionForward(
      hipdnn,
      &alpha, in_desc, src, filt_desc, weights,
      conv_desc, algo, ws_data, ws_size,
      &beta, out_desc, dst));

  hipdnnLRNDescriptor_t LRNDesc;
  hipdnnCreateLRNDescriptor(
           &LRNDesc);
  
  unsigned lrnN = 1;
double lrnAlpha=1, lrnBeta=1, lrnK=1;
hipdnnLRNMode_t LRN_mode = HIPDNN_LRN_CROSS_CHANNEL;


hipdnnSetLRNDescriptor(
   LRNDesc,
   LRN_mode,
    lrnN,
    lrnAlpha,
   lrnBeta,
   lrnK);
  
checkHIPDNN(hipdnnLRNCrossChannelForward( hipdnn, LRNDesc,LRN_mode, &alpha, out_desc,dst,&beta, out_desc,dst));

  high_resolution_timer_t timer;
    std::vector<double> time_vector(benchmark_iterations, 0);
    for (int i = 0; i < benchmark_iterations; i++) {

       timer.restart();
        checkHIPDNN(hipdnnLRNCrossChannelBackward( hipdnn, LRNDesc,LRN_mode, &alpha, out_desc, dst, out_desc, dst, in_desc, src, &beta, out_desc,dst));
       hipDeviceSynchronize();
        std::uint64_t time_elapsed = timer.elapsed_nanoseconds();
        time_vector[i] = (double)time_elapsed / 1000;
	}
	
	*avg_time = (float)std::accumulate(time_vector.begin() + 10, time_vector.end(), 0) / (benchmark_iterations - 10);
	
  // finalizing
  hipdnnDestroyTensorDescriptor(out_desc);
  hipdnnDestroyConvolutionDescriptor(conv_desc);
  hipdnnDestroyFilterDescriptor(filt_desc);
  hipdnnDestroyTensorDescriptor(in_desc);
  hipdnnDestroy(hipdnn);
 
}


#endif //TEST_LRN_BACKWARD_HPP