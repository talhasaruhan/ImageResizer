#include "dependencies/OpenCV/modules/core/include/opencv2/core/base.hpp"
#include "dependencies/OpenCV/modules/core/include/opencv2/core/types.hpp"
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

#include <cassert>
#include <iostream>
#include <vector>
#include <regex>
#include <string>
#include <string_view>
#include <filesystem>
#include <opencv2/core.hpp>
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
};

enum class EReturnCode
{
	OK,
	FILE_UNKNOWN_EXTENSION,
	FILE_READ_ERROR,
	FILE_WRITE_ERROR,
	UNKNOWN_ERROR,
};

void LogReturnCode(EReturnCode err, uint32_t verbose)
{
	if (!verbose)
	{
		return;
	}

	switch (err)
	{
	case EReturnCode::OK:
		std::cout << "OK\n";
		break;
	case EReturnCode::FILE_UNKNOWN_EXTENSION:
		if (verbose > 1)
		{
			std::cout << "File extension unknown (), skipping file:!\n";
			break;
		}
	case EReturnCode::FILE_READ_ERROR:
		std::cout << "ERROR: cannot read file:!\n";
		break;
	case EReturnCode::FILE_WRITE_ERROR:
		std::cout << "ERROR: cannot write file (source: , destination: )!\n";
		break;
	case EReturnCode::UNKNOWN_ERROR:
	default:
		std::cout << "ERROR: unknown error!\n";
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
		if (number_of_input_entries == 1)
		{
			fs::path output_path(path);

			if (output_path.has_parent_path())
			{
				output_path.make_preferred();
				std::string output_path_str = output_path.string();

				// If there's only one argument, the prefix is going to be common
				// amongs all the outputs, so remove it
				output_path_str = output_path_str.substr(
				    output_path_str.find((char)fs::path::preferred_separator) + 1);

				replace_str(output_path_str, separator, "_@_");

				return (fs::path(output_folder) / fs::path(output_path_str)).string();
			}
			else
			{
				return (output_folder / output_path).string();
			}
		}
		else
		{
			fs::path output_path(path);
			output_path.make_preferred();
			std::string output_path_str = output_path.string();
			replace_str(output_path_str, separator, "_@_");

			return (fs::path(output_folder) / fs::path(output_path_str)).string();
		}
	}
	else if (output_format == EOutputFormat::RECREATE_FOLDER_STRUCTURE)
	{
		const fs::path input_path(path);
		fs::path output_directory = output_folder / input_path.parent_path();
		fs::path output_path = output_folder / input_path;
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

EReturnCode ProcessFileImpl(const std::string_view path, const SProgramOptions& program_options)
{
	// Read the file
	cv::String path_ = cv::String(std::string(path));
	const cv::Mat image = cv::imread(path_);

	if (!image.data)
	{
		return EReturnCode::FILE_READ_ERROR;
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
		return EReturnCode::FILE_WRITE_ERROR;
	}

	return EReturnCode::OK;
}

EReturnCode ProcessFile(const std::string_view path, const SProgramOptions& program_options)
{
	const fs::path extension = fs::path(path).extension();
	ExtLookup::iterator ext_lut_it = g_ext_lookup_table.find(extension.string());

	if (ext_lut_it == g_ext_lookup_table.end())
	{
		return EReturnCode::FILE_UNKNOWN_EXTENSION;
	}

	EReturnCode err{EReturnCode::UNKNOWN_ERROR};

	switch (ext_lut_it->second)
	{
	// Fallthrough
	case EFileType::IMAGE_JPEG:
	case EFileType::IMAGE_PNG:
		err = ProcessFileImpl(path, program_options);
		break;
	case EFileType::OTHER:
	default:
		err = EReturnCode::FILE_UNKNOWN_EXTENSION;
		break;
	}

	LogReturnCode(err, program_options.verbose);

	return err;
}

void ProcessFolder(const fs::path& path, const SProgramOptions& program_options)
{
	for (const fs::path& entry : fs::directory_iterator(path))
	{
		const std::string entry_str_ = entry.string();
		const std::string_view entry_str = entry_str_;

		if (fs::is_regular_file(entry))
		{
			ProcessFile(entry_str, program_options);
		}
		else if (fs::is_directory(entry) && program_options.recursive)
		{
			// It's OK to recurse because it's very unlikely to have a directory
			// structure that will cause the stack to blow up
			ProcessFolder(entry_str, program_options);
		}
	}
}

void ProcessEntries(const std::vector<std::string>& arg_entries,
                    const SProgramOptions& program_options)
{
	for (const std::string& entry : arg_entries)
	{
		const fs::path entry_path(entry);

		if (fs::is_directory(entry_path))
		{
			ProcessFolder(entry_path, program_options);
		}
		else if (fs::is_regular_file(entry_path))
		{
			ProcessFile(entry, program_options);
		}
	}
}

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
	                 "0(only fatal errors) / 1(all errors, default) / 2(all).")
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
