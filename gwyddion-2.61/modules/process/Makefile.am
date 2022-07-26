# $Id: Makefile.am 24703 2022-03-21 15:51:51Z yeti-dn $

# Put sources that are for some reason disabled yet present in svn checkouts
# here.  This makes them distributed and translations of their messages are
# kept.  If we remove them completely, various bits can get lost.
disabled_sources = \
	superresolution.c

processdir = $(pkglibdir)/modules/process

# Only bundle modules without own dependencies.  If something goes wrong with
# any required libraries at run-time, it will at most break these individual
# unbundled modules, not the bundle.
bundlable_modules = \
	acf2d.la \
	angle_dist.la \
	anneal_synth.la \
	arc-revolve.la \
	arithmetic.la \
	averaging.la \
	basicops.la \
	bdep_synth.la \
	binning.la \
	calcoefs_load.la \
	calcoefs_new.la \
	calcoefs_simple.la \
	calcoefs_view.la \
	calibrate.la \
	classify.la \
	coerce.la \
	col_synth.la \
	convolution_filter.la \
	convolve.la \
	correct_affine.la \
	correct_perspective.la \
	cpde_synth.la \
	crosscor.la \
	curvature.la \
	cwt.la \
	deconvolve.la \
	displfield.la \
	deposit_synth.la \
	diff_synth.la \
	disc_synth.la \
	domain_synth.la \
	drift.la \
	dwt.la \
	dwtanisotropy.la \
	edge.la \
	entropy.la \
	extend.la \
	extract_path.la \
	facet-level.la \
	facet_analysis.la \
	facet_measure.la \
	fbm_synth.la \
	fft.la \
	fft_filter_1d.la \
	fft_filter_2d.la \
	psdf2d.la \
	fft_synth.la \
	fibre_synth.la \
	fit-shape.la \
	flatten_base.la \
	fraccor.la \
	fractal.la \
	freq_split.la \
	gaussian_step.la \
	good_profile.la \
	gradient.la \
	grain_cross.la \
	grain_dist.la \
	grain_edge.la \
	grain_filter.la \
	grain_makesel.la \
	grain_mark.la \
	grain_stat.la \
	grain_summary.la \
	grain_wshed.la \
	hertz.la \
	hough.la \
	immerse.la \
	indent_analyze.la \
	laplace.la \
	latsim.la \
	lat_synth.la \
	level.la \
	level_grains.la \
	linecorrect.la \
	linematch.la \
	lno_synth.la \
	local_contrast.la \
	logistic.la \
	mark_disconn.la \
	mark_with.la \
	maskcor.la \
	mask_distribute.la \
	mask_edt.la \
	mask_morph.la \
	mask_noisify.la \
	mask_shift.la \
	maskops.la \
	mcrop.la \
	measure_lattice.la \
	median-bg.la \
	merge.la \
	mfm_current.la \
	mfm_field.la \
	mfm_findshift.la \
	mfm_parallel.la \
	mfm_recalc.la \
	mfm_shift.la \
	multiprofile.la \
	neural.la \
	noise_synth.la \
	obj_synth.la \
	otsu_threshold.la \
	outliers.la \
	pat_synth.la \
	phase_synth.la \
	pid.la \
	pileup_synth.la \
	polylevel.la \
	polydistort.la \
	presentationops.la \
	psdf_logphi.la \
	psf.la \
	psf-fit.la \
	rank.la \
	rank_filter.la \
	raveraging.la \
	relate.la \
	resample.la \
	roddeposit_synth.la \
	rotate.la \
	semsim.la \
	scale.la \
	scars.la \
	shade.la \
	slope_dist.la \
	sphere-revolve.la \
	stitch.la \
	straighten_path.la \
	terracefit.la \
	threshold.la \
	tilt.la \
	tip_blind.la \
	tip_model.la \
	tipops.la \
	tipshape.la \
	trimmed-mean.la \
	volumize.la \
	volumize_layers.la \
	wave_synth.la \
	wfr_synth.la \
	wpour_mark.la \
	wrapvalue.la \
	xydenoise.la \
	xyz_channels.la \
	xyzize.la \
	zero_crossing.la

# process_modules is either the bundle or all the individual modules.
if MODULE_BUNDLING
process_modules = process.la
BUILT_SOURCES = process.c
process_la_CFLAGS = $(AM_CFLAGS) -DGWY_MODULE_BUNDLING
else
process_modules = $(bundlable_modules)
endif

process_LTLIBRARIES = \
	$(process_modules)

EXTRA_DIST = \
	$(disabled_sources)

if OS_MSWIN
no_undefined = -no-undefined
endif

if MODULE_DEPENDENCIES
module_libadd = \
	$(top_builddir)/app/libgwyapp2.la \
	$(top_builddir)/libdraw/libgwydraw2.la \
	$(top_builddir)/libgwyddion/libgwyddion2.la \
	$(top_builddir)/libgwydgets/libgwydgets2.la \
	$(top_builddir)/libgwymodule/libgwymodule2.la \
	$(top_builddir)/libprocess/libgwyprocess2.la \
	@GTK_LIBS@
endif

AM_CPPFLAGS = -I$(top_srcdir) -DG_LOG_DOMAIN=\"Module\"
AM_CFLAGS = @COMMON_CFLAGS@ @OPENMP_CFLAGS@
AM_LDFLAGS = -avoid-version -module $(no_undefined) $(module_libadd) @OPENMP_CFLAGS@

# Bundlable modules, must be listed in process_la_SOURCES below.
acf2d_la_SOURCES               = acf2d.c
angle_dist_la_SOURCES          = angle_dist.c preview.h
anneal_synth_la_SOURCES        = anneal_synth.c preview.h
arc_revolve_la_SOURCES         = arc-revolve.c preview.h
arithmetic_la_SOURCES          = arithmetic.c preview.h
averaging_la_SOURCES           = averaging.c preview.h
basicops_la_SOURCES            = basicops.c
bdep_synth_la_SOURCES          = bdep_synth.c preview.h
binning_la_SOURCES             = binning.c preview.h
calcoefs_load_la_SOURCES       = calcoefs_load.c preview.h
calcoefs_new_la_SOURCES        = calcoefs_new.c preview.h
calcoefs_simple_la_SOURCES     = calcoefs_simple.c
calcoefs_view_la_SOURCES       = calcoefs_view.c preview.h
calibrate_la_SOURCES           = calibrate.c preview.h
classify_la_SOURCES            = classify.c
coerce_la_SOURCES              = coerce.c preview.h
col_synth_la_SOURCES           = col_synth.c preview.h
convolution_filter_la_SOURCES  = convolution_filter.c convolutionfilterpreset.h preview.h
convolve_la_SOURCES            = convolve.c
correct_affine_la_SOURCES      = correct_affine.c preview.h
correct_perspective_la_SOURCES = correct_perspective.c preview.h
cpde_synth_la_SOURCES          = cpde_synth.c preview.h
crosscor_la_SOURCES            = crosscor.c
cwt_la_SOURCES                 = cwt.c preview.h
curvature_la_SOURCES           = curvature.c preview.h
deconvolve_la_SOURCES          = deconvolve.c preview.h
displfield_la_SOURCES          = displfield.c preview.h
deposit_synth_la_SOURCES       = deposit_synth.c dimensions.h preview.h
diff_synth_la_SOURCES          = diff_synth.c preview.h
disc_synth_la_SOURCES          = disc_synth.c preview.h
domain_synth_la_SOURCES        = domain_synth.c preview.h
drift_la_SOURCES               = drift.c preview.h
dwt_la_SOURCES                 = dwt.c preview.h
dwtanisotropy_la_SOURCES       = dwtanisotropy.c
edge_la_SOURCES                = edge.c preview.h
entropy_la_SOURCES             = entropy.c preview.h
extend_la_SOURCES              = extend.c preview.h
extract_path_la_SOURCES        = extract_path.c
facet_analysis_la_SOURCES      = facet_analysis.c preview.h
facet_measure_la_SOURCES       = facet_measure.c preview.h
facet_level_la_SOURCES         = facet-level.c preview.h
fbm_synth_la_SOURCES           = fbm_synth.c preview.h
fft_filter_1d_la_SOURCES       = fft_filter_1d.c preview.h
fft_filter_2d_la_SOURCES       = fft_filter_2d.c preview.h
fft_la_SOURCES                 = fft.c preview.h
fft_synth_la_SOURCES           = fft_synth.c preview.h
fibre_synth_la_SOURCES         = fibre_synth.c preview.h
fit_shape_la_SOURCES           = fit-shape.c preview.h
flatten_base_la_SOURCES        = flatten_base.c
fractal_la_SOURCES             = fractal.c preview.h
fraccor_la_SOURCES             = fraccor.c
freq_split_la_SOURCES          = freq_split.c preview.h
gaussian_step_la_SOURCES       = gaussian_step.c preview.h
good_profile_la_SOURCES        = good_profile.c
gradient_la_SOURCES            = gradient.c
grain_cross_la_SOURCES         = grain_cross.c
grain_dist_la_SOURCES          = grain_dist.c
grain_edge_la_SOURCES          = grain_edge.c
grain_filter_la_SOURCES        = grain_filter.c preview.h
grain_mark_la_SOURCES          = grain_mark.c preview.h
grain_makesel_la_SOURCES       = grain_makesel.c
grain_stat_la_SOURCES          = grain_stat.c
grain_summary_la_SOURCES       = grain_summary.c
grain_wshed_la_SOURCES         = grain_wshed.c preview.h
hertz_la_SOURCES               = hertz.c
hough_la_SOURCES               = hough.c preview.h
immerse_la_SOURCES             = immerse.c preview.h
indent_analyze_la_SOURCES      = indent_analyze.c preview.h
laplace_la_SOURCES             = laplace.c
lat_synth_la_SOURCES           = lat_synth.c preview.h
latsim_la_SOURCES              = latsim.c preview.h
level_la_SOURCES               = level.c preview.h
level_grains_la_SOURCES        = level_grains.c
linecorrect_la_SOURCES         = linecorrect.c
linematch_la_SOURCES           = linematch.c preview.h
lno_synth_la_SOURCES           = lno_synth.c preview.h
local_contrast_la_SOURCES      = local_contrast.c preview.h
logistic_la_SOURCES            = logistic.c
mark_disconn_la_SOURCES        = mark_disconn.c preview.h
mark_with_la_SOURCES           = mark_with.c preview.h
maskcor_la_SOURCES             = maskcor.c
mask_distribute_la_SOURCES     = mask_distribute.c preview.h
mask_edt_la_SOURCES            = mask_edt.c
mask_morph_la_SOURCES          = mask_morph.c
mask_noisify_la_SOURCES        = mask_noisify.c
mask_shift_la_SOURCES          = mask_shift.c
maskops_la_SOURCES             = maskops.c
mcrop_la_SOURCES               = mcrop.c
measure_lattice_la_SOURCES     = measure_lattice.c preview.h
median_bg_la_SOURCES           = median-bg.c preview.h
merge_la_SOURCES               = merge.c
mfm_current_la_SOURCES         = mfm_current.c mfmops.h synth.h
mfm_field_la_SOURCES           = mfm_field.c mfmops.h
mfm_findshift_la_SOURCES       = mfm_findshift.c mfmops.h
mfm_parallel_la_SOURCES        = mfm_parallel.c mfmops.h synth.h
mfm_recalc_la_SOURCES          = mfm_recalc.c mfmops.h
mfm_shift_la_SOURCES           = mfm_shift.c mfmops.h
multiprofile_la_SOURCES        = multiprofile.c preview.h
neural_la_SOURCES              = neural.c neuraldata.h
noise_synth_la_SOURCES         = noise_synth.c preview.h
obj_synth_la_SOURCES           = obj_synth.c preview.h
otsu_threshold_la_SOURCES      = otsu_threshold.c
outliers_la_SOURCES            = outliers.c
pat_synth_la_SOURCES           = pat_synth.c preview.h
phase_synth_la_SOURCES         = phase_synth.c preview.h
pid_la_SOURCES                 = pid.c preview.h
pileup_synth_la_SOURCES        = pileup_synth.c preview.h
polylevel_la_SOURCES           = polylevel.c preview.h
polydistort_la_SOURCES         = polydistort.c preview.h
presentationops_la_SOURCES     = presentationops.c
psdf2d_la_SOURCES              = psdf2d.c preview.h
psdf_logphi_la_SOURCES         = psdf_logphi.c preview.h
psf_la_SOURCES                 = psf.c preview.h mfmops.h
psf_fit_la_SOURCES             = psf-fit.c
rank_la_SOURCES                = rank.c preview.h
rank_filter_la_SOURCES         = rank-filter.c preview.h
raveraging_la_SOURCES          = raveraging.c
relate_la_SOURCES              = relate.c
resample_la_SOURCES            = resample.c
#resolution_la_SOURCES         = resolution.c
roddeposit_synth_la_SOURCES    = roddeposit_synth.c dimensions.h preview.h
rotate_la_SOURCES              = rotate.c preview.h
scale_la_SOURCES               = scale.c preview.h
scars_la_SOURCES               = scars.c preview.h
semsim_la_SOURCES              = semsim.c preview.h
shade_la_SOURCES               = shade.c preview.h
slope_dist_la_SOURCES          = slope_dist.c preview.h
sphere_revolve_la_SOURCES      = sphere-revolve.c preview.h
stitch_la_SOURCES              = stitch.c
straighten_path_la_SOURCES     = straighten_path.c preview.h
#superresolution_la_SOURCES    = superresolution.c
terracefit_la_SOURCES          = terracefit.c preview.h
threshold_la_SOURCES           = threshold.c preview.h
tilt_la_SOURCES                = tilt.c preview.h
tip_blind_la_SOURCES           = tip_blind.c preview.h
tip_model_la_SOURCES           = tip_model.c preview.h
tipops_la_SOURCES              = tipops.c
tipshape_la_SOURCES            = tipshape.c
trimmed_mean_la_SOURCES        = trimmed-mean.c
volumize_la_SOURCES            = volumize.c
volumize_layers_la_SOURCES     = volumize_layers.c preview.h
wave_synth_la_SOURCES          = wave_synth.c preview.h
wfr_synth_la_SOURCES           = wfr_synth.c preview.h
wpour_mark_la_SOURCES          = wpour_mark.c preview.h
wrapvalue_la_SOURCES           = wrapvalue.c preview.h
xydenoise_la_SOURCES           = xydenoise.c
xyz_channels_la_SOURCES        = xyz_channels.c
xyzize_la_SOURCES              = xyzize.c
zero_crossing_la_SOURCES       = zero_crossing.c preview.h

process_la_SOURCES = \
	$(acf2d_la_SOURCES) \
	$(angle_dist_la_SOURCES) \
	$(anneal_synth_la_SOURCES) \
	$(arc_revolve_la_SOURCES) \
	$(arithmetic_la_SOURCES) \
	$(averaging_la_SOURCES) \
	$(basicops_la_SOURCES) \
	$(bdep_synth_la_SOURCES) \
	$(binning_la_SOURCES) \
	$(calcoefs_load_la_SOURCES) \
	$(calcoefs_new_la_SOURCES) \
	$(calcoefs_simple_la_SOURCES) \
	$(calcoefs_view_la_SOURCES) \
	$(calibrate_la_SOURCES) \
	$(classify_la_SOURCES) \
	$(coerce_la_SOURCES) \
	$(col_synth_la_SOURCES) \
	$(convolution_filter_la_SOURCES) \
	$(convolve_la_SOURCES) \
	$(correct_affine_la_SOURCES) \
	$(correct_perspective_la_SOURCES) \
	$(cpde_synth_la_SOURCES) \
	$(crosscor_la_SOURCES) \
	$(cwt_la_SOURCES) \
	$(curvature_la_SOURCES) \
	$(deconvolve_la_SOURCES) \
	$(displfield_la_SOURCES) \
	$(deposit_synth_la_SOURCES) \
	$(diff_synth_la_SOURCES) \
	$(disc_synth_la_SOURCES) \
	$(domain_synth_la_SOURCES) \
	$(drift_la_SOURCES) \
	$(dwt_la_SOURCES) \
	$(dwtanisotropy_la_SOURCES) \
	$(edge_la_SOURCES) \
	$(entropy_la_SOURCES) \
	$(extend_la_SOURCES) \
	$(extract_path_la_SOURCES) \
	$(facet_analysis_la_SOURCES) \
	$(facet_measure_la_SOURCES) \
	$(facet_level_la_SOURCES) \
	$(fbm_synth_la_SOURCES) \
	$(fft_filter_1d_la_SOURCES) \
	$(fft_filter_2d_la_SOURCES) \
	$(fft_la_SOURCES) \
	$(fft_synth_la_SOURCES) \
	$(fibre_synth_la_SOURCES) \
	$(fit_shape_la_SOURCES) \
	$(flatten_base_la_SOURCES) \
	$(fractal_la_SOURCES) \
	$(fraccor_la_SOURCES) \
	$(freq_split_la_SOURCES) \
	$(gaussian_step_la_SOURCES) \
	$(good_profile_la_SOURCES) \
	$(gradient_la_SOURCES) \
	$(grain_cross_la_SOURCES) \
	$(grain_dist_la_SOURCES) \
	$(grain_edge_la_SOURCES) \
	$(grain_filter_la_SOURCES) \
	$(grain_makesel_la_SOURCES) \
	$(grain_mark_la_SOURCES) \
	$(grain_stat_la_SOURCES) \
	$(grain_summary_la_SOURCES) \
	$(grain_wshed_la_SOURCES) \
	$(hertz_la_SOURCES) \
	$(hough_la_SOURCES) \
	$(immerse_la_SOURCES) \
	$(indent_analyze_la_SOURCES) \
	$(laplace_la_SOURCES) \
	$(lat_synth_la_SOURCES) \
	$(latsim_la_SOURCES) \
	$(level_la_SOURCES) \
	$(level_grains_la_SOURCES) \
	$(linecorrect_la_SOURCES) \
	$(linematch_la_SOURCES) \
	$(lno_synth_la_SOURCES) \
	$(local_contrast_la_SOURCES) \
	$(logistic_la_SOURCES) \
	$(mark_disconn_la_SOURCES) \
	$(mark_with_la_SOURCES) \
	$(maskcor_la_SOURCES) \
	$(mask_distribute_la_SOURCES) \
	$(mask_edt_la_SOURCES) \
	$(mask_morph_la_SOURCES) \
	$(mask_noisify_la_SOURCES) \
	$(mask_shift_la_SOURCES) \
	$(maskops_la_SOURCES) \
	$(mcrop_la_SOURCES) \
	$(measure_lattice_la_SOURCES) \
	$(median_bg_la_SOURCES) \
	$(merge_la_SOURCES) \
	$(mfm_current_la_SOURCES) \
	$(mfm_field_la_SOURCES) \
	$(mfm_findshift_la_SOURCES) \
	$(mfm_parallel_la_SOURCES) \
	$(mfm_recalc_la_SOURCES) \
	$(mfm_shift_la_SOURCES) \
	$(multiprofile_la_SOURCES) \
	$(neural_la_SOURCES) \
	$(noise_synth_la_SOURCES) \
	$(obj_synth_la_SOURCES) \
	$(otsu_threshold_la_SOURCES) \
	$(outliers_la_SOURCES) \
	$(pat_synth_la_SOURCES) \
	$(phase_synth_la_SOURCES) \
	$(pid_la_SOURCES) \
	$(pileup_synth_la_SOURCES) \
	$(polylevel_la_SOURCES) \
	$(polydistort_la_SOURCES) \
	$(presentationops_la_SOURCES) \
	$(psdf2d_la_SOURCES) \
	$(psdf_logphi_la_SOURCES) \
	$(psf_la_SOURCES) \
	$(psf_fit_la_SOURCES) \
	$(rank_la_SOURCES) \
	$(rank_filter_la_SOURCES) \
	$(raveraging_la_SOURCES) \
	$(relate_la_SOURCES) \
	$(resample_la_SOURCES) \
	$(roddeposit_synth_la_SOURCES) \
	$(rotate_la_SOURCES) \
	$(scale_la_SOURCES) \
	$(scars_la_SOURCES) \
	$(semsim_la_SOURCES) \
	$(shade_la_SOURCES) \
	$(slope_dist_la_SOURCES) \
	$(sphere_revolve_la_SOURCES) \
	$(stitch_la_SOURCES) \
	$(straighten_path_la_SOURCES) \
	$(terracefit_la_SOURCES) \
	$(threshold_la_SOURCES) \
	$(tilt_la_SOURCES) \
	$(tip_blind_la_SOURCES) \
	$(tip_model_la_SOURCES) \
	$(tipops_la_SOURCES) \
	$(tipshape_la_SOURCES) \
	$(trimmed_mean_la_SOURCES) \
	$(volumize_la_SOURCES) \
	$(volumize_layers_la_SOURCES) \
	$(wave_synth_la_SOURCES) \
	$(wfr_synth_la_SOURCES) \
	$(wpour_mark_la_SOURCES) \
	$(wrapvalue_la_SOURCES) \
	$(xydenoise_la_SOURCES) \
	$(xyz_channels_la_SOURCES) \
	$(xyzize_la_SOURCES) \
	$(zero_crossing_la_SOURCES) \
	process.c

process.c: $(srcdir)/Makefile.am $(top_srcdir)/utils/gen-module-bundle.sh
	$(SHELL) $(top_srcdir)/utils/gen-module-bundle.sh process.c $(bundlable_modules)

CLEAN_FILES = process.c

clean-local:
	rm -f core.* *~

