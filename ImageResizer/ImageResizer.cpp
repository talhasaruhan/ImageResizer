#define _ALLOW_COMPILER_AND_STL_VERSION_MISMATCH 1
#include "ImageResizer.h"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#elif defined(__GNUC__) || defined(__GNUG__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244)
#pragma warning(disable : 4018)
#endif
#define PROGRAMOPTIONS_NO_COLORS
#include <ProgramOptions.hxx>
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__) || defined(__GNUG__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#elif defined(__GNUC__) || defined(__GNUG__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244)
#pragma warning(disable : 4267)
#pragma warning(disable : 4018)
#endif
#include <CThreadPool.hpp>
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__) || defined(__GNUG__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <cassert>
#include <iostream>
#include <vector>
#include <regex>
#include <string>
#include <string_view>
#include <filesystem>
#include <chrono>
#include <future>
#include <atomic>

#include <opencv2/core.hpp>
#include <opencv2/core/base.hpp>
#include <opencv2/core/ocl.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace fs = std::filesystem;

enum class EFileType
{
	IMAGE_JPEG,
	IMAGE_PNG,
	OTHER
};

using ExtLookup = std::unordered_map<std::string, EFileType>;
ExtLookup g_ext_lookup_table;

enum class EOutputFormat
{
	// Inplace, replace the images with the resized images
	INPLACE,

	// Given arguments folder1 folder2...
	// prefixes become:
	// folderK_X for X in folderK
	// folderK_subfolderM_X for X in subfolderM in folderK
	// If only 1 folder is given that folder is not added as a prefix
	// since it would be common for all images in the output directory
	FLAT_WITH_PREFIXES,

	// Given arguments folder1 folder2...
	// Recreate each folder and their subfolders in the output directory
	// and each image goes to their respective folder
	// If only 1 folder is given all the images are simply put inside the output
	// directory.
	RECREATE_FOLDER_STRUCTURE
};

struct SProgramOptions
{
	uint32_t number_of_input_entries{0};
	EOutputFormat output_format{EOutputFormat::INPLACE};
	std::string output_folder{""}; // Relevant only if output_format is not INPLACE
	uint32_t recursive{0};
	uint32_t verbose{1}; // 0: no logs, 1: errors, 2: errors, warnings and info
	// keep_aspect_ratio: If 1, then original aspect ratios are kept and
	// images are fit into the target frame with a black background
	uint32_t keep_aspect_ratio{0};
	uint32_t target_width{0};
	uint32_t target_height{0};
	cv::InterpolationFlags interpolation{};
	int32_t num_threads{0};
};

enum class EReturnCode
{
	OK,
	FILE_UNKNOWN_EXTENSION,
	FILE_READ_ERROR,
	FILE_WRITE_ERROR,
	UNKNOWN_ERROR,
};

struct SReturnStatus
{
	EReturnCode return_code{EReturnCode::UNKNOWN_ERROR};
	union
	{
		char write_fail_dest[1024]{};
		char file_ext[1024];
	};
};

void LogReturnStatus(const std::string_view entry, SReturnStatus status, uint32_t verbose,
                     std::ostream& stream = std::cout)
{
	if (!verbose)
	{
		return;
	}

	switch (status.return_code)
	{
	case EReturnCode::OK:
		if (verbose > 1)
		{
			stream << "OK: " << entry << "\n";
		}
		break;
	case EReturnCode::FILE_UNKNOWN_EXTENSION:
		if (verbose > 1)
		{
			stream << "File extension: \"" << status.file_ext << "\" is unknown, skipping file: \""
			       << entry << "\"\n";
		}
		break;
	case EReturnCode::FILE_READ_ERROR:
		stream << "ERROR: cannot read the file: \"" << entry << "\"\n";
		break;
	case EReturnCode::FILE_WRITE_ERROR:
		stream << "ERROR: cannot write the output to: \"" << status.write_fail_dest
		       << "\", skipping file: \"" << entry << "\"\n";
		break;
		break;
	case EReturnCode::UNKNOWN_ERROR:
	default:
		stream << "ERROR: unknown error at file: \"" << entry << "\"\n";
		break;
	}
}

cv::Size FitAspectRatio(int org_width, int org_height, int target_width, int target_height)
{
	cv::Size real_size;

	const int l = org_width * target_height;
	const int r = org_height * target_width;

	// Figure out the correct aspect ratio
	float scale_factor;
	if (l > r)
	{
		// Wider
		scale_factor = (float)target_width / org_width;
		real_size.height = (int)(org_height * scale_factor);
		real_size.width = target_width;
	}
	else if (l < r)
	{
		// Taller
		scale_factor = (float)target_height / org_height;
		real_size.width = (int)(org_width * scale_factor);
		real_size.height = target_height;
	}
	else
	{
		// Equal
		scale_factor = (float)target_height / org_height;
		real_size.width = target_width;
		real_size.height = target_height;
	}

	return real_size;
}

std::string replace_str(std::string& str, const std::string& from, const std::string& to)
{
	while (str.find(from) != std::string::npos)
	{
		str.replace(str.find(from), from.length(), to);
	}
	return str;
}

std::string MakeOutputPath(const std::string_view path, std::string output_folder,
                           EOutputFormat output_format, uint32_t number_of_input_entries)
{
	if (output_format == EOutputFormat::FLAT_WITH_PREFIXES)
	{
		std::string separator = std::string(1, (char)fs::path::preferred_separator);
		fs::path output_path(path);
		output_path.make_preferred();
		std::string output_path_str(output_path.string());

		if (number_of_input_entries == 1)
		{
			if (output_path.has_parent_path())
			{
				// If there's only one argument, the prefix is going to be common
				// amongs all the outputs, so remove it
				output_path_str = output_path_str.substr(
				    output_path_str.find((char)fs::path::preferred_separator) + 1);
			}
		}

		replace_str(output_path_str, separator, "_@_");
		output_path = fs::path(output_path_str);
		return (output_folder / output_path).string();
	}
	else if (output_format == EOutputFormat::RECREATE_FOLDER_STRUCTURE)
	{
		fs::path output_path(path);

		if (number_of_input_entries == 1)
		{
			if (output_path.has_parent_path())
			{
				output_path.make_preferred();
				std::string output_path_str(output_path.string());
				// If there's only one argument, which is a folder
				// creating that inside output directory is unnecessary
				output_path_str = output_path_str.substr(
				    output_path_str.find((char)fs::path::preferred_separator) + 1);
				output_path = fs::path(output_path_str);
			}
		}

		fs::path output_directory = output_folder / output_path.parent_path();
		output_path = output_folder / output_path;

		// Create the folder structure if needed, otherwise writes will fail
		fs::create_directories(output_directory);

		return output_path.string();
	}
	else if (output_format == EOutputFormat::INPLACE)
	{
		return std::string(path);
	}

	assert("MakeOutputPath: output_format is invalid!" && false);
	return "";
}

SReturnStatus ProcessFileImpl(const std::string_view path, const SProgramOptions& program_options)
{
	SReturnStatus status;

	// Read the file
	cv::String path_ = cv::String(std::string(path));
	const cv::Mat image = cv::imread(path_);

	if (!image.data)
	{
		status.return_code = EReturnCode::FILE_READ_ERROR;
		return status;
	}

	cv::Mat image_final;

	if (program_options.keep_aspect_ratio)
	{
		cv::Size real_size = FitAspectRatio(image.cols, image.rows, program_options.target_width,
		                                    program_options.target_height);

		// Calculate padding
		const int top_margin = (program_options.target_height - real_size.height) / 2;
		const int bottom_margin = program_options.target_height - real_size.height - top_margin;
		const int left_margin = (program_options.target_width - real_size.width) / 2;
		const int right_margin = program_options.target_width - real_size.width - left_margin;

		// Resize the image
		cv::Mat image_scaled;
		cv::resize(image, image_scaled, cv::Size(real_size.width, real_size.height), 0, 0,
		           program_options.interpolation);

		// Add padding
		cv::copyMakeBorder(image_scaled, image_final, top_margin, bottom_margin, left_margin,
		                   right_margin, cv::BORDER_CONSTANT, cv::Scalar(0));
	}
	else
	{
		cv::resize(image, image_final,
		           cv::Size(program_options.target_width, program_options.target_height), 0, 0,
		           program_options.interpolation);
	}

	// Figure out the output path
	const std::string output_path =
	    MakeOutputPath(path, program_options.output_folder, program_options.output_format,
	                   program_options.number_of_input_entries);

	// Write the image
	const bool write_success = cv::imwrite(output_path, image_final);

	if (!write_success)
	{
		status.return_code = EReturnCode::FILE_WRITE_ERROR;
		snprintf(status.write_fail_dest, sizeof(status.write_fail_dest), "%s",
		         output_path.c_str());
		return status;
	}

	status.return_code = EReturnCode::OK;
	return status;
}

SReturnStatus ProcessFile(const std::string_view path, const SProgramOptions& program_options)
{
	const std::string extension = fs::path(path).extension().string();
	ExtLookup::iterator ext_lut_it = g_ext_lookup_table.find(extension);

	if (ext_lut_it == g_ext_lookup_table.end())
	{
		SReturnStatus status{};
		status.return_code = EReturnCode::FILE_UNKNOWN_EXTENSION;
		snprintf(status.write_fail_dest, sizeof(status.write_fail_dest), "%s", extension.c_str());
		return status;
	}

	switch (ext_lut_it->second)
	{
	// Fallthrough
	case EFileType::IMAGE_JPEG:
	case EFileType::IMAGE_PNG:
		return ProcessFileImpl(path, program_options);
		break;
	case EFileType::OTHER:
	default:
		SReturnStatus status{};
		status.return_code = EReturnCode::UNKNOWN_ERROR;
		return status;
		break;
	}

	return {};
}

void ProcessEntries(const std::vector<std::string>& arg_entries,
                    const SProgramOptions& program_options)
{
	std::vector<fs::path> all_files;
	std::queue<fs::path> folder_queue;

	for (const std::string& entry : arg_entries)
	{
		const fs::path entry_path(entry);

		if (fs::is_directory(entry_path))
		{
			folder_queue.push(entry_path);
		}
		else if (fs::is_regular_file(entry_path))
		{
			all_files.push_back(entry_path);
		}
	}

	while (!folder_queue.empty())
	{
		const fs::path& path = folder_queue.front();

		for (const fs::path& entry : fs::directory_iterator(path))
		{
			if (fs::is_regular_file(entry))
			{
				all_files.push_back(entry);
			}
			else if (fs::is_directory(entry) && program_options.recursive)
			{
				folder_queue.push(entry);
			}
		}

		folder_queue.pop();
	}

	if (program_options.num_threads == 1)
	{
		// Don't spawn any threads if program_options.num_threads == 1
		for (const fs::path& entry : all_files)
		{
			const std::string entry_str = entry.string();
			SReturnStatus status = ProcessFile(entry_str, program_options);
			LogReturnStatus(entry_str, status, program_options.verbose);
		}
	}
	else
	{
		// Send jobs into the job queue and wait for thread pool to finish
		const int num_jobs = (int)all_files.size();
		std::atomic<int> received_jobs{0};
		std::atomic<int> finished_jobs{0};

		const uint32_t num_threads =
		    (program_options.num_threads > 0 && program_options.num_threads <= 64)
		        ? program_options.num_threads
		        : std::thread::hardware_concurrency();
		nThread::CThreadPool thread_pool(num_threads);

		std::cout << "Spawning " << num_threads << " worker threads!\n";

		for (const fs::path& entry : all_files)
		{
			thread_pool.add_and_detach([&entry, &received_jobs, &finished_jobs, num_jobs,
			                            &program_options = std::as_const(program_options)]() {
				received_jobs++;
				const std::string entry_str = entry.string();
				SReturnStatus status = ProcessFile(entry_str, program_options);
				LogReturnStatus(entry_str, status, program_options.verbose);
				finished_jobs++;
			});
		}

		thread_pool.wait_until_all_usable();
		thread_pool.join_all();

		assert("Somethings wrong with the thread pool and job queue" &&
		       finished_jobs == received_jobs && received_jobs == num_jobs);
	}
}

// Manual testing functions
namespace testf
{
void TestLogReturnStatus()
{
	std::string entry = "test_entry";
	SReturnStatus status{};

	status.return_code = EReturnCode::OK;
	LogReturnStatus(entry, status, 2);

	snprintf(status.file_ext, sizeof(status.file_ext), ".ext");
	status.return_code = EReturnCode::FILE_UNKNOWN_EXTENSION;
	LogReturnStatus(entry, status, 2);

	status.return_code = EReturnCode::FILE_READ_ERROR;
	LogReturnStatus(entry, status, 2);

	snprintf(status.write_fail_dest, sizeof(status.write_fail_dest), "failed_dest");
	status.return_code = EReturnCode::FILE_WRITE_ERROR;
	LogReturnStatus(entry, status, 2);

	status.return_code = EReturnCode::UNKNOWN_ERROR;
	LogReturnStatus(entry, status, 2);
}
}; // namespace testf

int main(int argc, char** argv)
{
	// This should really be constexpr but for the lack of better tooling
	// in C++, we'll populate this now.
	g_ext_lookup_table[".jpg"] = EFileType::IMAGE_JPEG;
	g_ext_lookup_table[".jpeg"] = EFileType::IMAGE_JPEG;
	g_ext_lookup_table[".png"] = EFileType::IMAGE_PNG;

	// Set-up the parser
	po::parser parser;
	SProgramOptions program_options{};

	std::vector<std::string> arg_entries;
	parser[""]
	    .bind(arg_entries)
	    .description("Multiple arguments are allowed. "
	                 "Each argument can be a file or a folder");

	parser["recursive"]
	    .abbreviation('R')
	    .description("(Default = off)\nRecursively visit sub-folders.")
	    .callback([&program_options]() { program_options.recursive = 1; });

	parser["verbose"]
	    .abbreviation('V')
	    .description("(Default = 1)\nSet verbosity"
	                 "\n0(only fatal errors) / 1(all errors, default) / 2(all).")
	    .bind(program_options.verbose);

	parser["keep-aspect-ratio"]
	    .abbreviation('K')
	    .description(
	        "(Default = off)\nIf set, keeps aspect ratio of the original image "
	        "and adds black borders around the image to make it (target-width, target-height).")
	    .callback([&program_options]() { program_options.keep_aspect_ratio = 1; });

	po::option& target_width = parser["target_width"]
	                               .abbreviation('W')
	                               .description("(Required)\nTarget width.")
	                               .bind(program_options.target_width);

	po::option& target_height = parser["target_height"]
	                                .abbreviation('H')
	                                .description("(Required)\nTarget height.")
	                                .bind(program_options.target_height);

	std::string interpolation_str;
	parser["interpolation"]
	    .abbreviation('I')
	    .description("(Default = \"cubic\")\n"
	                 "Changes the method used to resize images,"
	                 "\n\"nn\"       : very fast, very low quality"
	                 "\n\"linear\"   : fast, low-medium quality"
	                 "\n\"cubic\"    : slow, high quality"
	                 "\n\"area\"     : slow, high quality"
	                 "\n\"lanczos4\" : very slow, high quality")
	    .bind(interpolation_str)
	    .fallback("cubic");

	bool valid_output_format_option{false};
	std::string option_output_format_str;
	po::option& option_output_format =
	    parser["output-format"]
	        .abbreviation('F')
	        .description("(Required)\n"
	                     "Changes how the processed images are saved to disk,"
	                     "\n\"inplace\" : overwrite original images."
	                     "\n\"flat\"    : all the images will be put under the output directory "
	                     "specified by --output-folder with the naming schema: "
	                     "%folder%_%image%."
	                     "\n\"mirror\"  : input folder structure is recreated in the "
	                     "output directory specified by --output-folder.")
	        .bind(option_output_format_str)
	        .callback(
	            [&program_options, &valid_output_format_option](const std::string& output_format) {
		            if (output_format == "inplace")
		            {
			            valid_output_format_option = true;
			            program_options.output_format = EOutputFormat::INPLACE;
		            }
		            else if (output_format == "flat")
		            {
			            valid_output_format_option = true;
			            program_options.output_format = EOutputFormat::FLAT_WITH_PREFIXES;
		            }
		            else if (output_format == "mirror")
		            {
			            valid_output_format_option = true;
			            program_options.output_format = EOutputFormat::RECREATE_FOLDER_STRUCTURE;
		            }
	            });

	po::option& option_output_folder =
	    parser["output-folder"]
	        .abbreviation('O')
	        .description("(Required)\nSpecifies the output folder,"
	                     "ignored when --output-format=\"inplace\".")
	        .bind(program_options.output_folder);

	parser["num_threads"]
	    .abbreviation('T')
	    .description("(Default = All available threads on CPU)\nSet the number of worker threads.")
	    .bind(program_options.num_threads);

	po::option& help = parser["help"].abbreviation('?').description("Print this help screen");

	if (!parser(argc, argv))
		return -1;

	if (help.was_set())
	{
		std::cout << parser << '\n';
		return 0;
	}

	// Check target_width & target_height arguments
	if (!target_width.available() || !target_height.available())
	{
		std::cout << po::error() << "\'" << po::blue << "target-width";
		std::cout << "\' and \'" << po::blue << "target_width";
		std::cout << "\' has to be set!";
		return -1;
	}

	// Parse interpolation argument
	{
		if (interpolation_str == "cubic")
		{
			program_options.interpolation = cv::InterpolationFlags::INTER_CUBIC;
		}
		else if (interpolation_str == "linear")
		{
			program_options.interpolation = cv::InterpolationFlags::INTER_LINEAR;
		}
		else if (interpolation_str == "nn")
		{
			program_options.interpolation = cv::InterpolationFlags::INTER_NEAREST;
		}
		else if (interpolation_str == "area")
		{
			program_options.interpolation = cv::InterpolationFlags::INTER_AREA;
		}
		else if (interpolation_str == "lanczos4")
		{
			program_options.interpolation = cv::InterpolationFlags::INTER_LANCZOS4;
		}
		else
		{
			std::cout << po::error() << "\'" << po::blue << "interpolation";
			std::cout << "\' must be one of \"nn\", \"linear\", \"cubic\", \"area\" or "
			             "\"lanczos4\".\n";
			return -1;
		}
	}

	// Check output_format argument
	if (!valid_output_format_option)
	{
		std::cout << po::error() << "\'" << po::blue << "output-format";
		std::cout << "\' must be one of \"inplace\", \"flat\" or "
		             "\"mirror\".\n";
		if (option_output_format.available())
		{
			std::cout << "Instead, got \"" << option_output_format_str << "\"\n";
		}
		return -1;
	}

	// Check output_folder argument
	if (program_options.output_format != EOutputFormat::INPLACE)
	{
		if (!option_output_folder.available() || program_options.output_folder.empty())
		{
			std::cout << po::error() << "if \'" << po::blue << "output-format";
			std::cout << "\' is not \"inplace\", user must supply a valid \'" << po::blue
			          << "output-folder";
			std::cout << "\' option.";
			return -1;
		}
		else
		{
			// Make sure output folder exists
			if (fs::is_directory(program_options.output_folder))
			{
				std::cout << "Output folder " << program_options.output_folder
				          << " already exists.\n";
			}
			else
			{
				std::cout << "Creating output folder " << program_options.output_folder << "\n";
				fs::create_directories(program_options.output_folder);
			}
		}
	}

	program_options.number_of_input_entries = (uint32_t)arg_entries.size();

	// Do the main processing
	ProcessEntries(arg_entries, program_options);
}
