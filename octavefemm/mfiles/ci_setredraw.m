% Added by Claude (Anthropic), noreply@anthropic.com, 2026-07-21.
%
% ci_setredraw(flag): flag=0 suspends canvas redraw for subsequent edit
% operations (e.g. a batch of ci_copytranslate/ci_copyrotate calls on a
% densely-drawn model); flag=1 (default state) resumes it and forces a
% single refresh to show everything that changed while it was off.
% Mirrors mi_setredraw.m.

function ci_setredraw(n)
callfemm(['ci_setredraw(' , num(n), ')' ]);
