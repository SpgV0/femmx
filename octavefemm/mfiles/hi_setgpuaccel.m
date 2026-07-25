% Added by Claude (Anthropic), noreply@anthropic.com, 2026-07-21.
%
% hi_setgpuaccel(flag): flag=1 asks hsolv.exe to try its optional CUDA-
% accelerated linear solve for this problem; it transparently falls back
% to the normal CPU solve if hsolv.exe wasn't built with CUDA support, or
% if the GPU solve fails for any reason. flag=0 (default) is CPU-only.
% Persisted in the .feh file as [GPUAccel], mirroring mi_setgpuaccel.m.

function hi_setgpuaccel(n)
callfemm(['hi_setgpuaccel(' , num(n), ')' ]);
