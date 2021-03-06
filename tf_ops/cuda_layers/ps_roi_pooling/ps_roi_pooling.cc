//
// Created by yifeng on 18-8-5.
//

#define EIGEN_USE_THREADS

#include <cfloat>
#include <vector>


#include "ps_roi_pooling.h"
#include "tensorflow/core/framework/op.h"
#include "tensorflow/core/framework/shape_inference.h"

//#include "third_party/eigen3/unsupported/Eigen/CXX11/Tensor"
//// #include "tensorflow/core/common_runtime/device.h"
//#include "tensorflow/core/framework/op_kernel.h"
//#include "tensorflow/core/framework/op.h"
//#include "tensorflow/core/framework/numeric_op.h"
//#include "tensorflow/core/framework/register_types.h"
#include "tensorflow/core/framework/tensor.h"
#include "tensorflow/core/framework/tensor_shape.h"
//#include "tensorflow/core/framework/tensor_slice.h"
#include "tensorflow/core/framework/common_shape_fns.h"
//#include "tensorflow/core/lib/core/errors.h"
//#include "tensorflow/core/lib/gtl/array_slice.h"
//#include "tensorflow/core/util/padding.h"
#include "tensorflow/core/framework/shape_inference.h"
//#include "tensorflow/core/util/tensor_format.h"
//#include "tensorflow/core/kernels/bounds_check.h"
//#include "tensorflow/core/kernels/bounds_check.h"
//#include "tensorflow/core/platform/stream_executor.h"


//handle forward pass
namespace tensorflow {
    using shape_inference::DimensionHandle;
    using shape_inference::InferenceContext;
    using shape_inference::ShapeHandle;

    REGISTER_OP("PsRoiPooling")
            .Input("bottom_data: T")
            .Input("bottom_rois: T")
            .Attr("spatial_scale: float")
            .Attr("output_dim: int")
            .Attr("group_size: int")
            .Attr("data_format: string")
            .Attr("T: {float, double}")
            .Output("output: T")
            .SetShapeFn([](InferenceContext *c) {
                // do dimension checking here
                return Status::OK();
            })
            .Doc(R"doc(xxx)doc");

    extern template struct PSROIPoolingFunctor_FW_GPU<float>;
    extern template struct PSROIPoolingFunctor_FW_GPU<double>;

    template <typename T>
    class PS_ROI_POOLING_FW_GPU : public OpKernel {
        public:
            explicit PS_ROI_POOLING_FW_GPU(OpKernelConstruction* context) : OpKernel(context) {
                // get parameters from outside
                OP_REQUIRES_OK(context, context->GetAttr("spatial_scale", &spatial_scale_));
                OP_REQUIRES_OK(context, context->GetAttr("output_dim", &output_dim_));
                OP_REQUIRES(context, output_dim_ > 0, errors::InvalidArgument( "output_dim must be > 0"));
                OP_REQUIRES_OK(context, context->GetAttr("group_size", &group_size_));
                OP_REQUIRES(context, group_size_ > 0, errors::InvalidArgument( "group_size must be > 0"));

                string data_format;
                OP_REQUIRES_OK(context, context->GetAttr("data_format", &data_format));
                OP_REQUIRES(context, FormatFromString(data_format, &data_format_),
                            errors::InvalidArgument("Invalid data format"));
            }

            void Compute(OpKernelContext* context) override {
                const Tensor& bottom_data = context->input(0);
                const TensorShape& bottom_data_shape = bottom_data.shape();
                const Tensor& bottom_rois = context->input(1);
                const TensorShape& bottom_rois_shape = bottom_rois.shape();

                height_ =  static_cast<int>(GetTensorDim(bottom_data, data_format_, 'H'));
                width_ = static_cast<int>(GetTensorDim(bottom_data, data_format_, 'W'));
                channels_ = static_cast<int>(GetTensorDim(bottom_data, data_format_, 'C'));

                OP_REQUIRES(context, channels_ == output_dim_*group_size_*group_size_,
                            errors::InvalidArgument( "input channel number does not match layer parameters"));

                pooled_height_ = group_size_;
                pooled_width_ = group_size_;

                roi_nums_ = static_cast<int>(bottom_rois.dim_size(0));
                //allocate spaces for top_data
                Tensor* top_data;
                TensorShape top_shape = ShapeFromFormat(data_format_, roi_nums_, pooled_height_, pooled_width_, output_dim_);
                OP_REQUIRES_OK(context, context->allocate_output(0, top_shape, &top_data));

//                // allocate spaces for mapping_channel_, which is needed when BP
//                Tensor mapping_channel_;
//                TensorShape mapping_channel_shape = ShapeFromFormat(data_format_, roi_nums_, pooled_height_, pooled_width_, output_dim_);
//                OP_REQUIRES_OK(context, context->allocate_temp(DataTypeToEnum<int>::value, mapping_channel_shape, &mapping_channel_));
//                auto mapping_channel_ptr = mapping_channel_.template flat<int>().data();

                auto top_data_ptr = top_data->template flat<T>().data();
                auto bottom_data_ptr = bottom_data.template flat<T>().data();
                auto bottom_rois_ptr = bottom_rois.template flat<T>().data();

                // now call gpu kernel here!
                PSROIPoolingFunctor_FW_GPU<T>()(
                        context->eigen_device<GPUDevice>(),
                        bottom_data_ptr,
                        bottom_rois_ptr,
                        spatial_scale_,
                        roi_nums_, channels_,
                        height_, width_,
                        pooled_height_, pooled_width_,
                        output_dim_,
                        group_size_,
                        top_data_ptr
                );
            }

        private:
//            void LayerSetUp(){
//                //calculate useful parameters from those given while calling constructor
//            }
            // define params here
            float spatial_scale_;
            int channels_;
            int output_dim_;
            int roi_nums_;
            int group_size_;
            int height_;
            int width_;
            int pooled_height_;
            int pooled_width_;
            TensorFormat data_format_;
    };

    #ifdef GOOGLE_CUDA
        #define REGISTER_FW_GPU(T)                                          \
          /* Declare explicit instantiations in kernel_example.cu.cc. */ \
          REGISTER_KERNEL_BUILDER(                                       \
              Name("PsRoiPooling").Device(DEVICE_GPU).TypeConstraint<T>("T"), \
              PS_ROI_POOLING_FW_GPU<T>);
        REGISTER_FW_GPU(float);
        REGISTER_FW_GPU(double);
    #endif  // GOOGLE_CUDA
};


//handle bp
namespace tensorflow {
    using shape_inference::DimensionHandle;
    using shape_inference::InferenceContext;
    using shape_inference::ShapeHandle;

    REGISTER_OP("PsRoiPoolingBp")
            .Input("top_diff: T")
            .Input("bottom_data: T")
            .Input("bottom_rois: T")
            .Attr("spatial_scale: float")
            .Attr("output_dim: int")
            .Attr("group_size: int")
            .Attr("data_format: string")
            .Attr("T: {float, double}")
            .Output("bottom_diff: T")
            .SetShapeFn([](InferenceContext *c) {
                // do dimension checking here
                return Status::OK();
            })
            .Doc(R"doc(xxx)doc");

    extern template struct PSROIPoolingFunctor_BP_GPU<float>;
    extern template struct PSROIPoolingFunctor_BP_GPU<double>;
    extern template struct setZero_GPU<float>;
    extern template struct setZero_GPU<double>;

    template <typename T>
    class PS_ROI_POOLING_BP_GPU : public OpKernel {
    public:
        explicit PS_ROI_POOLING_BP_GPU(OpKernelConstruction* context) : OpKernel(context) {
            // get parameters from outside
            OP_REQUIRES_OK(context, context->GetAttr("spatial_scale", &spatial_scale_));
            OP_REQUIRES_OK(context, context->GetAttr("output_dim", &output_dim_));
            OP_REQUIRES(context, output_dim_ > 0, errors::InvalidArgument( "output_dim must be > 0"));
            OP_REQUIRES_OK(context, context->GetAttr("group_size", &group_size_));
            OP_REQUIRES(context, group_size_ > 0, errors::InvalidArgument( "group_size must be > 0"));
            string data_format;
            OP_REQUIRES_OK(context, context->GetAttr("data_format", &data_format));
            OP_REQUIRES(context, FormatFromString(data_format, &data_format_),
                        errors::InvalidArgument("Invalid data format"));
        }

        void Compute(OpKernelContext* context) override {
            const Tensor& top_diff = context->input(0);
            const TensorShape& top_diff_shape = top_diff.shape();
            const Tensor& bottom_data = context->input(1);
            const TensorShape& bottom_data_shape = bottom_data.shape();
            const Tensor& bottom_rois = context->input(2);
            const TensorShape& bottom_rois_shape = bottom_rois.shape();


            batch_size_ =  static_cast<int>(GetTensorDim(bottom_data, data_format_, 'N'));
            height_ =  static_cast<int>(GetTensorDim(bottom_data, data_format_, 'H'));
            width_ = static_cast<int>(GetTensorDim(bottom_data, data_format_, 'W'));
            channels_ = static_cast<int>(GetTensorDim(bottom_data, data_format_, 'C'));

            OP_REQUIRES(context, channels_ == output_dim_*group_size_*group_size_,
                        errors::InvalidArgument( "input channel number does not match layer parameters"));

            pooled_height_ = group_size_;
            pooled_width_ = group_size_;

            roi_nums_ = static_cast<int>(bottom_rois.dim_size(0));
            //allocate spaces for top_data
            Tensor* bottom_diff;
            TensorShape top_shape = ShapeFromFormat(data_format_, batch_size_, height_, width_, channels_);
            OP_REQUIRES_OK(context, context->allocate_output(0, top_shape, &bottom_diff));

//                // allocate spaces for mapping_channel_, which is needed when BP
//                Tensor mapping_channel_;
//                TensorShape mapping_channel_shape = ShapeFromFormat(data_format_, roi_nums_, pooled_height_, pooled_width_, output_dim_);
//                OP_REQUIRES_OK(context, context->allocate_temp(DataTypeToEnum<int>::value, mapping_channel_shape, &mapping_channel_));
//                auto mapping_channel_ptr = mapping_channel_.template flat<int>().data();

            auto top_diff_ptr = top_diff.template flat<T>().data();
            auto bottom_data_ptr = bottom_data.template flat<T>().data();
            auto bottom_rois_ptr = bottom_rois.template flat<T>().data();
            auto bottom_diff_ptr = bottom_diff->template flat<T>().data();

            setZero_GPU<T>()(context->eigen_device<GPUDevice>(),
                    batch_size_* height_* width_* channels_,
                       bottom_diff_ptr);

            PSROIPoolingFunctor_BP_GPU<T>()(
                    context->eigen_device<GPUDevice>(),
                    top_diff_ptr,
                    bottom_rois_ptr,
                    spatial_scale_,
                    roi_nums_, channels_,
                    height_, width_,
                    pooled_height_, pooled_width_,
                    output_dim_, bottom_diff_ptr
            );
            // now call gpu kernel here!
        }

    private:
//            void LayerSetUp(){
//                //calculate useful parameters from those given while calling constructor
//            }
// define params here
        float spatial_scale_;
        int channels_;
        int batch_size_;
        int output_dim_;
        int roi_nums_;
        int group_size_;
        int height_;
        int width_;
        int pooled_height_;
        int pooled_width_;
        TensorFormat data_format_;
    };

#ifdef GOOGLE_CUDA
#define REGISTER_BP_GPU(T)                                          \
          /* Declare explicit instantiations in kernel_example.cu.cc. */ \
          REGISTER_KERNEL_BUILDER(                                       \
              Name("PsRoiPoolingBp").Device(DEVICE_GPU).TypeConstraint<T>("T"), \
              PS_ROI_POOLING_BP_GPU<T>);
    REGISTER_BP_GPU(float);
    REGISTER_BP_GPU(double);
#endif  // GOOGLE_CUDA
};

