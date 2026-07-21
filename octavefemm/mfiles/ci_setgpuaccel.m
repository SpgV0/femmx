% Added by Claude (Anthropic), noreply@anthropic.com, 2026-07-21.
%
% ci_setgpuaccel(flag): flag=1 asks csolv.exe to try its optional
% CUDA-accelerated linear solve for this problem; it transparently falls
% back to the normal CPU solve if csolv.exe wasn't built with CUDA
% support, or if the GPU solve fails for any reason. flag=0 (default) is
% CPU-only. Persisted in the .fec file as [GPUAccel], mirroring
% mi_setgpuaccel.m.

function ci_setgpuaccel(n)
callfemm(['ci_setgpuaccel(' , num(n), ')' ]);
