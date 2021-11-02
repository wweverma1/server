# include file for test files that can be run with and without debug
# having debug and non-debug tests.
let $have_debug=`select version() like '%debug%'`;

if ($MTR_COMBINATION_VERS)
{
  if (!$have_debug)
  {
    --skip Requires debug
  }
}
