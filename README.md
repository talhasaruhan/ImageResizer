Usage:
  ImageResizer.exe [options] [arguments...]

Arguments:
  Multiple arguments are allowed. Each argument can be a file or a folder

Available options:

    -R, --recursive            (Default = off)
                               Recursively visit sub-folders.

    -V, --verbose              (Default = 1)
                               Set verbosity0(only fatal errors) / 1(all errors, default) / 2(all).

    -K, --keep-aspect-ratio    (Default = off)
                               If set, keeps aspect ratio of the original image and adds black bor-
                               ders around the image to make it (target-width, target-height).

    -W, --target_width         (Required)
                               Target width.

    -H, --target_height        (Required)
                               Target height.

    -I, --interpolation        (Default = "cubic")
                               Changes the method used to resize images,
                               "nn"       : very fast, very low quality
                               "linear"   : fast, low-medium quality
                               "cubic"    : slow, high quality
                               "area"     : slow, high quality
                               "lanczos4" : very slow, high quality

    -F, --output-format        (Required)
                               Changes how the processed images are saved to disk,
                               "inplace" : overwrite original images.
                               "flat"    : all the images will be put under the output directory s-
                               pecified by --output-folder with the naming schema: %folder%_%image-
                               %.
                               "mirror"  : input folder structure is recreated in the output direc-
                               tory specified by --output-folder.

    -O, --output-folder        (Required)
                               Specifies the output folder,ignored when --output-format="inplace".

    -?, --help                 Print this help screen


