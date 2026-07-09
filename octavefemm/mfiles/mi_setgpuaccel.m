% Added by Claude (Anthropic), noreply@anthropic.com, 2026-07-09.
%
% mi_setgpuaccel(flag): flag=1 asks fkn.exe to try its optional CUDA-
% accelerated linear solve for this problem; it transparently falls back
% to the normal CPU solve if fkn.exe wasn't built with CUDA support, or
% if the GPU solve fails for any reason. flag=0 (default) is CPU-only.
% Persisted in the .fem file as [GPUAccel], same mechanism as the
% existing AC solver choice.

function mi_setgpuaccel(n)
callfemm(['mi_setgpuaccel(' , num(n), ')' ]);
