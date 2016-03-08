
#include "catch.hpp"
#include "yuri/cuda/Cuda.h"
#include <numeric>


namespace yuri{

__global__
void iota(int *a)
{
	a[threadIdx.x] = threadIdx.x;
}


__global__
void cpy(int *a, int* b)
{
	b[threadIdx.x] = a[threadIdx.x];
}


TEST_CASE("cuda") {
	std::vector<int> a(10, 0);
	std::vector<int> b(10, 0);
	std::vector<int> expected(10, 0);
	std::iota(expected.begin(), expected.end(), 0);
	
	auto mem_a = cuda::cuda_alloc<int>(a.size());
	auto mem_b = cuda::cuda_alloc<int>(b.size());
	dim3 dimBlock( a.size(), 1 );
	dim3 dimGrid( 1, 1 );
			
	SECTION("CUDA COPY") {
		REQUIRE(cuda::copy_to_gpu(mem_a, a));

		iota<<<dimGrid, dimBlock>>>(mem_a.get());
		
		REQUIRE(cuda::copy_to_cpu(a, mem_a));
		
		REQUIRE(a == expected);
		REQUIRE(a != b);
		
		REQUIRE(cuda::copy_to_gpu(mem_b, b));
		
		cpy<<<dimGrid, dimBlock>>>(mem_a.get(), mem_b.get());
		
		REQUIRE(cuda::copy_to_cpu(b, mem_b));
		REQUIRE(a == b);
	}
	
		
}
}
		