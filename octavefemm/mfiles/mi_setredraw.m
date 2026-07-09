% Added by Claude (Anthropic), noreply@anthropic.com, 2026-07-09.
%
% mi_setredraw(flag): flag=0 suspends canvas redraw for subsequent edit
% operations (e.g. a batch of mi_copytranslate/mi_copyrotate calls on a
% densely-drawn model); flag=1 (default state) resumes it and forces a
% single refresh to show everything that changed while it was off.

function mi_setredraw(n)
callfemm(['mi_setredraw(' , num(n), ')' ]);
