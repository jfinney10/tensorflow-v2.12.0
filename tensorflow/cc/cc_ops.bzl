 def tf_cc_ops_hdrs(
         name,
         op_lib_names,
         visibility):
     subhdrs = []
     internalhdrs = []
     for n in op_lib_names:
         subhdrs += ["ops/" + n + ".h"]
         internalhdrs += ["ops/" + n + "_internal.h"]
 
     native.filegroup(
         name = name,
         srcs = subhdrs + internalhdrs,
         visibility = visibility,
     )
