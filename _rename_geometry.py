import re, glob

# All .h and .cpp in Geometry
files = glob.glob('src/Geometry/**/*.h', recursive=True) + \
        glob.glob('src/Geometry/**/*.cpp', recursive=True)

# Method renames: (old_name, new_name)
# Only snake_case multi-word identifiers
renames = [
    # Traits - ParametricCurve
    (r'\bparameter_range\b',         'parameterRange'),
    (r'\brange_tuple\b',             'rangeTuple'),
    (r'\bparameter_division\b',      'parameterDivision'),
    (r'\btransform_by\b',            'transformBy'),
    (r'\bsearch_parameter\b',        'searchParameter'),
    (r'\bsearch_nearest_parameter\b','searchNearestParameter'),
    (r'\bnormal_uder\b',             'normalUDer'),
    (r'\bnormal_vder\b',             'normalVDer'),
    (r'\bu_period\b',                'uPeriod'),
    (r'\bv_period\b',                'vPeriod'),
    (r'\bder_mn\b',                  'derMN'),
    (r'\bder_n\b',                   'derN'),
    # Specified types
    (r'\bfront_point\b',             'frontPoint'),
    (r'\bback_point\b',              'backPoint'),
    # NURBS
    (r'\bbspline_basis_functions\b',  'bsplineBasisFunctions'),
    (r'\btry_bspline_basis_functions\b','tryBsplineBasisFunctions'),
    (r'\bis_clamped\b',              'isClamped'),
    (r'\bsame_range\b',              'sameRange'),
    (r'\brange_length\b',            'rangeLength'),
    (r'\badd_knot\b',                'addKnot'),
    (r'\bremove_knot\b',             'removeKnot'),
    (r'\bnew_checked\b',             'newChecked'),
    (r'\btry_new\b',                 'tryNew'),
    (r'\brat_ders\b',                'ratDers'),
    # Tolerance
    (r'\bso_small\b',                'soSmall'),
    # BoundingBox member fields
    (r'\bmin_pt\b',                  'minPt'),
    (r'\bmax_pt\b',                  'maxPt'),
    # Decorators
    (r'\bgenerating_curve\b',        'generatingCurve'),
    (r'\bextrusion_vector\b',        'extrusionVector'),
    (r'\brotation_origin\b',         'rotationOrigin'),
    (r'\brotation_axis\b',           'rotationAxis'),
    (r'\binner_curve\b',             'innerCurve'),
    (r'\bfront_param\b',             'frontParam'),
    (r'\bback_param\b',              'backParam'),
    (r'\bparameter_curve\b',         'parameterCurve'),
    (r'\bhost_surface\b',            'hostSurface'),
    # Surface partials
    (r'\bmaximum_order\b',           'maximumOrder'),
    (r'\bcontrol_points\b',          'controlPoints'),
    (r'\bknot_vec\b',                'knotVec'),
    (r'\bapply_transform\b',         'applyTransform'),
    (r'\bto_same_geometry\b',        'toSameGeometry'),
    (r'\binclude_curve\b',           'includeCurve'),
    (r'\bu_degree\b',                'uDegree'),
    (r'\bv_degree\b',                'vDegree'),
    (r'\buknot_vec\b',               'uKnotVec'),
    (r'\bvknot_vec\b',               'vKnotVec'),
    (r'\bnon_rationalized\b',        'nonRationalized'),
    (r'\btry_bspline_basis\b',       'tryBsplineBasis'),
]

# First pass: collect all replacements
total = 0
for fpath in files:
    with open(fpath, 'r', encoding='utf-8') as f:
        content = f.read()
    
    original = content
    for pattern, replacement in renames:
        content, n = re.subn(pattern, replacement, content)
        total += n
    
    if content != original:
        with open(fpath, 'w', encoding='utf-8') as f:
            f.write(content)
        print(f'  Modified: {fpath}')

print(f'\nTotal replacements: {total}')
