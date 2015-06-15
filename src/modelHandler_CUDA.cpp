#include "CUDAlib.h"
#include "Buffer.hpp"
#include "params.h"
#include "filters.hpp"
#include "sec.hpp"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static HMODULE handle;
#else
#include <dlfcn.h>
static void *handle;
#endif

static const char prog20[] = 
#include "modelHandler_CUDA.ptx20.h"
	;
static const char prog30[] = 
#include "modelHandler_CUDA.ptx30.h"
	;


#define CUDA_DECL(name)				\
	t##name name;

FOR_EACH_CUDA_FUNC(CUDA_DECL, CUDA_DECL)

namespace w2xc {

static int
cudalib_init(void)
{
#ifdef _WIN32
	handle = LoadLibrary("nvcuda.dll");
#else
	handle = dlopen("libcuda.so.1", RTLD_LAZY);

#define GetProcAddress dlsym

#endif
	if (!handle) {
		return -1;
	}

#define LOAD(name)					\
	name = (t##name) GetProcAddress(handle, #name); \
	if (name == NULL) {				\
		return -1;				\
	}

#define LOAD_V2(name)					\
	name = (t##name) GetProcAddress(handle, #name "_v2"); \
	if (name == NULL) {				\
		return -1;				\
	}

	FOR_EACH_CUDA_FUNC(LOAD, LOAD_V2);

	return 0;
}


bool
initCUDA(ComputeEnv *env)
{
	if (cudalib_init() < 0) {
		return false;
	}

	CUresult r = cuInit(0);
	if (r != CUDA_SUCCESS) {
		return false;
	}

	int dev_count;
	cuDeviceGetCount(&dev_count);

	if (dev_count == 0) {
		return false;
	}

	CUcontext ctxt;
	CUdevice dev = 0;
	CUmodule mod;
	CUstream stream;

	r = cuStreamCreate(&stream, 0);

	r = cuCtxCreate(&ctxt, CU_CTX_SCHED_BLOCKING_SYNC, dev);
	if (r != CUDA_SUCCESS) {
		return false;
	}

	int cap_major;
	r = cuDeviceGetAttribute(&cap_major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, dev);
	if (r != CUDA_SUCCESS) {
		cuCtxDestroy(ctxt);
		return false;
	}

	const char *prog = prog20;
	if (cap_major >= 3) {
		prog = prog30;
	}

	r = cuStreamCreate(&stream, 0);
	if (r != CUDA_SUCCESS) {
		cuCtxDestroy(ctxt);
		return false;
	}

	CUjit_option jit_options[2];
	void *jit_optvals[2];

	jit_options[0] = CU_JIT_CACHE_MODE;
	jit_optvals[0] = (void*)(uintptr_t)CU_JIT_CACHE_OPTION_CA;

	//jit_options[1] = CU_JIT_OPTIMIZATION_LEVEL;
	//jit_optvals[1] = (void*)(uintptr_t)0;

	r = cuModuleLoadDataEx(&mod, prog20, 1, jit_options, jit_optvals);
	if (r != CUDA_SUCCESS) {
		cuCtxDestroy(ctxt);
		cuStreamDestroy(stream);
		return false;
	}

	CUfunction filter_i32=0, filter_i64=0, filter_i128=0;
	CUfunction filter_i64_o64=0, filter_i128_o128=0, filter_i64_o128=0;
	CUfunction filter_i128_o1=0;

	r = cuModuleGetFunction(&filter_i32, mod, "filter_i32");
	if (r != CUDA_SUCCESS) {
		cuModuleUnload(mod);
		cuCtxDestroy(ctxt);
		cuStreamDestroy(stream);
		return false;
	}
	r = cuModuleGetFunction(&filter_i64, mod, "filter_i64");
	if (r != CUDA_SUCCESS) {
		cuModuleUnload(mod);
		cuCtxDestroy(ctxt);
		cuStreamDestroy(stream);
		return false;
	}
	r = cuModuleGetFunction(&filter_i128, mod, "filter_i128");
	if (r != CUDA_SUCCESS) {
		cuModuleUnload(mod);
		cuCtxDestroy(ctxt);
		cuStreamDestroy(stream);
		return false;
	}
	r = cuModuleGetFunction(&filter_i64_o64, mod, "filter_i64_o64");
	if (r != CUDA_SUCCESS) {
		cuModuleUnload(mod);
		cuCtxDestroy(ctxt);
		cuStreamDestroy(stream);
		return false;
	}
	r = cuModuleGetFunction(&filter_i64_o128, mod, "filter_i64_o128");
	if (r != CUDA_SUCCESS) {
		cuModuleUnload(mod);
		cuCtxDestroy(ctxt);
		cuStreamDestroy(stream);
		return false;
	}
	r = cuModuleGetFunction(&filter_i128_o128, mod, "filter_i128_o128");
	if (r != CUDA_SUCCESS) {
		cuModuleUnload(mod);
		cuCtxDestroy(ctxt);
		cuStreamDestroy(stream);
		return false;
	}
	r = cuModuleGetFunction(&filter_i128_o1, mod, "filter_i128_o1");
	if (r != CUDA_SUCCESS) {
		cuModuleUnload(mod);
		cuCtxDestroy(ctxt);
		cuStreamDestroy(stream);
		return false;
	}

	char name [1024];
	cuDeviceGetName(name, sizeof(name), dev);
	printf("CUDA : %s\n", name);

	cuCtxSetCacheConfig(CU_FUNC_CACHE_PREFER_SHARED);
	cuCtxSetSharedMemConfig(CU_SHARED_MEM_CONFIG_FOUR_BYTE_BANK_SIZE);
	//cuFuncSetSharedMemConfig(filter_i32, CU_SHARED_MEM_CONFIG_EIGHT_BYTE_BANK_SIZE);
	//cuFuncSetSharedMemConfig(filter_i64, CU_SHARED_MEM_CONFIG_EIGHT_BYTE_BANK_SIZE);
	//cuFuncSetSharedMemConfig(filter_i128, CU_SHARED_MEM_CONFIG_EIGHT_BYTE_BANK_SIZE);

	env->num_cuda_dev = 1;
	env->cuda_dev_list = new CUDADev[1];
	env->cuda_dev_list[0].dev = dev;
	env->cuda_dev_list[0].context = ctxt;
	env->cuda_dev_list[0].mod = mod;
	env->cuda_dev_list[0].filter_i32 = filter_i32;
	env->cuda_dev_list[0].filter_i64 = filter_i64;
	env->cuda_dev_list[0].filter_i128 = filter_i128;
	env->cuda_dev_list[0].filter_i64_o64 = filter_i64_o64;
	env->cuda_dev_list[0].filter_i64_o128 = filter_i64_o128;
	env->cuda_dev_list[0].filter_i128_o128 = filter_i128_o128;
	env->cuda_dev_list[0].filter_i128_o1 = filter_i128_o1;
	env->cuda_dev_list[0].stream = stream;

	return true;
}


void
filter_CUDA_impl(ComputeEnv *env,
		 Buffer *packed_input_buf,
		 Buffer *packed_output_buf,
		 int nInputPlanes,
		 int nOutputPlanes,
		 const float *biases,
		 const float *weight,
		 int ip_width,
		 int ip_height,
		 int nJob)
{
	CUresult r;
	CUDADev *dev = &env->cuda_dev_list[0];
	CUdeviceptr packed_input = packed_input_buf->get_read_ptr_cuda(env, 0);
	CUdeviceptr packed_output = packed_output_buf->get_write_ptr_cuda(env, 0);

	cuCtxPushCurrent(dev->context);

	CUdeviceptr d_fbiases = 0;
	size_t bias_size = sizeof(float) * nOutputPlanes;
	r = cuMemAlloc(&d_fbiases, bias_size);
	if (r != CUDA_SUCCESS) {
		printf("fail: alloc bias %d.", (int)r);
		exit(1);
	}
	r = cuMemcpyHtoD(d_fbiases, biases, bias_size);
	if (r != CUDA_SUCCESS) {
		puts("fail: copy to bias");
		exit(1);
	}
	CUdeviceptr d_weight = 0;
	size_t weight_size = sizeof(float) * 128 * nInputPlanes * 9;
	r = cuMemAlloc(&d_weight, weight_size);
	if (r != CUDA_SUCCESS) {
		puts("fail: alloc weight");
		exit(1);
	}

	r = cuMemcpyHtoD(d_weight, weight, weight_size);
	if (r != CUDA_SUCCESS) {
		puts("fail: copy to weight");
		exit(1);
	}

	size_t nInputPlanes2 = nInputPlanes;
	size_t nOutputPlanes2 = nOutputPlanes;
	size_t h = ip_height;
	size_t w = ip_width;

	if ((nInputPlanes == 128 && nOutputPlanes == 128) ||
	    (nInputPlanes == 64 && nOutputPlanes == 128) ||
	    (nInputPlanes == 64 && nOutputPlanes == 64))
	{
		CUfunction f = 0;
		if (nInputPlanes == 128 && nOutputPlanes == 128) {
			f = dev->filter_i128_o128;
		} else if (nInputPlanes == 64 && nOutputPlanes == 128) {
			f = dev->filter_i64_o128;
		} else if (nInputPlanes == 64 && nOutputPlanes == 64) {
			f = dev->filter_i64_o64;
		}

		for (size_t ib0=0; ib0<(size_t)nInputPlanes; ib0+=32) {
			for (size_t ob0=0; ob0<(size_t)nOutputPlanes; ob0+=64) {
				void *args[] = {&packed_input,
						&packed_output,
						&d_fbiases,
						&h,
						&w,
						&d_weight,
						&ib0,
						&ob0};

				r = cuLaunchKernel(f,
						   h, 1, 1,
						   64, 1, 1,
						   0,
						   dev->stream, args, NULL);
				if (r != CUDA_SUCCESS) {
					puts("fail: launch");
					exit(1);
				}
			}
		}
	} else if (nInputPlanes == 128 && nOutputPlanes == 1) {
		void *args[8] = {&packed_input,
				 &packed_output,
				 &d_fbiases,
				 &h,
				 &w,
				 &d_weight};
		r = cuLaunchKernel(dev->filter_i128_o1,
				   h, 1, 1,
				   128, 1, 1,
				   0,
				   dev->stream, args, NULL);
	} else {
		void *args[8] = {&packed_input,
				 &packed_output,
				 &nOutputPlanes2,
				 &d_fbiases,
				 &h,
				 &w,
				 &d_weight};

		if (nInputPlanes == 32) {
			r = cuLaunchKernel(dev->filter_i32,
					   h, 1, 1,
					   nOutputPlanes, 1, 1,
					   sizeof(float) * nInputPlanes * (GPU_BLOCK_SIZE+2) * 3,
					   dev->stream, args, NULL);
		} else if (nInputPlanes == 64) {
			r = cuLaunchKernel(dev->filter_i64,
					   h, 1, 1,
					   nOutputPlanes, 1, 1,
					   sizeof(float) * nInputPlanes * (GPU_BLOCK_SIZE+2) * 3,
					   dev->stream, args, NULL);
		} else if (nInputPlanes == 128) {
			r = cuLaunchKernel(dev->filter_i128,
					   h, 1, 1,
					   nOutputPlanes, 1, 1,
					   sizeof(float) * nInputPlanes * (GPU_BLOCK_SIZE+2) * 3,
					   dev->stream, args, NULL);
		} else {
			abort();
		}
	}
	if (r != CUDA_SUCCESS) {
		puts("fail: launch");
		exit(1);
	}

	r = cuStreamSynchronize(dev->stream);
	if (r != CUDA_SUCCESS) {
		puts("fail: stream sync");
		exit(1);
	}

	cuMemFree(d_weight);
	cuMemFree(d_fbiases);

	CUcontext old;
	cuCtxPopCurrent(&old);
}


}