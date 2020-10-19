#include <iostream>

#include <opencv2/core.hpp>
#include <opencv2/core/base.hpp>
#include <opencv2/core/ocl.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>


int TestOpenCL()
{
	bool openclActivated = cv::ocl::useOpenCL();
	std::cout << "isOpenCLActivated() -> " << openclActivated << "\n";

	if (cv::ocl::haveOpenCL())
	{
		std::cout << "Have OpenCL\n";
	}
	else
	{
		std::cout << "Does NOT have OpenCL\n";
	}

	cv::ocl::Context context;
	if (!context.create(cv::ocl::Device::TYPE_GPU))
	{
		std::cout << "Failed creating the context...\n";
		return -1;
	}

	std::cout << context.ndevices() << " GPU devices are detected.\n"; //This bit provides an overview of the OpenCL devices you have in your computer
	for (int i = 0; i < context.ndevices(); i++)
	{
		cv::ocl::Device device = context.device(i);
		std::cout << "name:              " << device.name() << std::endl;
		std::cout << "available:         " << device.available() << std::endl;
		std::cout << "imageSupport:      " << device.imageSupport() << std::endl;
		std::cout << "OpenCL_C_Version:  " << device.OpenCL_C_Version() << std::endl;
		std::cout << std::endl;
	}

	openclActivated = cv::ocl::useOpenCL();
	std::cout << "isOpenCLActivated() -> " << openclActivated << "\n";

	cv::Mat image = cv::imread("image_1_s.jpg");

	if (!image.data)
	{
		std::cout << "Can't read image!\n";
		return -1;
	}

	auto t0 = std::chrono::high_resolution_clock::now();
	auto t1 = std::chrono::high_resolution_clock::now();

	cv::ocl::setUseOpenCL(1);
	openclActivated = cv::ocl::useOpenCL();
	std::cout << "isOpenCLActivated() -> " << openclActivated << "\n";
	std::cout << "cv::Mat:\n";
	t0 = std::chrono::high_resolution_clock::now();
	for (int i = 0; i < 5000; ++i)
	{
		cv::Mat dest;
		cv::resize(image, dest, cv::Size(480, 480), 0, 0, cv::InterpolationFlags::INTER_CUBIC);
	}
	t1 = std::chrono::high_resolution_clock::now();
	std::cout << "Time: " << std::chrono::duration<double>(t1-t0).count() * 1000 << " ms\n";

	cv::ocl::setUseOpenCL(0);
	openclActivated = cv::ocl::useOpenCL();
	std::cout << "isOpenCLActivated() -> " << openclActivated << "\n";
	std::cout << "cv::Mat:\n";
	t0 = std::chrono::high_resolution_clock::now();
	for (int i = 0; i < 5000; ++i)
	{
		cv::Mat dest;
		cv::resize(image, dest, cv::Size(480, 480), 0, 0, cv::InterpolationFlags::INTER_CUBIC);
	}
	t1 = std::chrono::high_resolution_clock::now();
	std::cout << "Time: " << std::chrono::duration<double>(t1-t0).count() * 1000 << " ms\n";

	cv::ocl::setUseOpenCL(1);
	openclActivated = cv::ocl::useOpenCL();
	std::cout << "isOpenCLActivated() -> " << openclActivated << "\n";
	std::cout << "cv::UMat:\n";
	cv::UMat uimage;
	cv::UMat udest;
	image.copyTo(uimage);
	uimage.copyTo(udest);
	t0 = std::chrono::high_resolution_clock::now();
	for (int i = 0; i < 5000; ++i)
	{
		cv::resize(uimage, udest, cv::Size(480, 480), 0, 0, cv::InterpolationFlags::INTER_CUBIC);
	}
	t1 = std::chrono::high_resolution_clock::now();
	std::cout << "Time: " << std::chrono::duration<double>(t1-t0).count() * 1000 << " ms\n";

	return 0;
}
