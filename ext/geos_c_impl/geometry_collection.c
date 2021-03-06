/*
  -----------------------------------------------------------------------------
  
  Geometry collection methods for GEOS wrapper
  
  -----------------------------------------------------------------------------
  Copyright 2010 Daniel Azuma
  
  All rights reserved.
  
  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:
  
  * Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.
  * Neither the name of the copyright holder, nor the names of any other
    contributors to this software, may be used to endorse or promote products
    derived from this software without specific prior written permission.
  
  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.
  -----------------------------------------------------------------------------
*/


#include "preface.h"

#ifdef RGEO_GEOS_SUPPORTED

#include <ruby.h>
#include <geos_c.h>

#include "factory.h"
#include "geometry.h"
#include "line_string.h"
#include "polygon.h"
#include "geometry_collection.h"

RGEO_BEGIN_C


/**** INTERNAL IMPLEMENTATION OF CREATE ****/


// Main implementation of the "create" class method for geometry collections.
// You must pass in the correct GEOS geometry type ID.

static VALUE create_geometry_collection(VALUE module, int type, VALUE factory, VALUE array)
{
  VALUE result = Qnil;
  Check_Type(array, T_ARRAY);
  unsigned int len = (unsigned int)RARRAY_LEN(array);
  GEOSGeometry** geoms = ALLOC_N(GEOSGeometry*, len == 0 ? 1 : len);
  if (geoms) {
    RGeo_FactoryData* factory_data = RGEO_FACTORY_DATA_PTR(factory);
    GEOSContextHandle_t geos_context = factory_data->geos_context;
    VALUE klass;
    unsigned int i,j;
    VALUE klasses = Qnil;
    VALUE cast_type = Qnil;
    switch (type) {
    case GEOS_MULTIPOINT:
      cast_type = factory_data->globals->feature_point;
      break;
    case GEOS_MULTILINESTRING:
      cast_type = factory_data->globals->feature_line_string;
      break;
    case GEOS_MULTIPOLYGON:
      cast_type = factory_data->globals->feature_polygon;
      break;
    }
    for (i=0; i<len; ++i) {
      GEOSGeometry* geom = rgeo_convert_to_detached_geos_geometry(rb_ary_entry(array, i), factory, cast_type, &klass);
      if (!geom) {
        break;
      }
      geoms[i] = geom;
      if (!NIL_P(klass) && NIL_P(klasses)) {
        klasses = rb_ary_new2(len);
        for (j=0; j<i; ++j) {
          rb_ary_push(klasses, Qnil);
        }
      }
      if (!NIL_P(klasses)) {
        rb_ary_push(klasses, klass);
      }
    }
    if (i != len) {
      for (j=0; j<i; ++j) {
        GEOSGeom_destroy_r(geos_context, geoms[j]);
      }
    }
    else {
      GEOSGeometry* collection = GEOSGeom_createCollection_r(geos_context, type, geoms, len);
      // Due to a limitation of GEOS, the MultiPolygon assertions are not checked.
      // We do that manually here.
      if (collection && type == GEOS_MULTIPOLYGON && (factory_data->flags & 1) == 0) {
        char problem = 0;
        for (i=1; i<len; ++i) {
          for (j=0; j<i; ++j) {
            GEOSGeometry* igeom = geoms[i];
            GEOSGeometry* jgeom = geoms[j];
            problem = GEOSRelatePattern_r(geos_context, igeom, jgeom, "2********");
            if (problem) {
              break;
            }
            problem = GEOSRelatePattern_r(geos_context, igeom, jgeom, "****1****");
            if (problem) {
              break;
            }
          }
          if (problem) {
            break;
          }
        }
        if (problem) {
          GEOSGeom_destroy_r(geos_context, collection);
          collection = NULL;
        }
      }
      if (collection) {
        result = rgeo_wrap_geos_geometry(factory, collection, module);
        RGEO_GEOMETRY_DATA_PTR(result)->klasses = klasses;
      }
      // NOTE: We are assuming that GEOS will do its own cleanup of the
      // element geometries if it fails to create the collection, so we
      // are not doing that ourselves. If that turns out not to be the
      // case, this will be a memory leak.
    }
    free(geoms);
  }
  
  return result;
}


/**** RUBY METHOD DEFINITIONS ****/


static VALUE method_geometry_collection_eql(VALUE self, VALUE rhs)
{
  VALUE result = rgeo_geos_klasses_and_factories_eql(self, rhs);
  if (RTEST(result)) {
    RGeo_GeometryData* self_data = RGEO_GEOMETRY_DATA_PTR(self);
    result = rgeo_geos_geometry_collections_eql(self_data->geos_context, self_data->geom, RGEO_GEOMETRY_DATA_PTR(rhs)->geom, RGEO_FACTORY_DATA_PTR(self_data->factory)->flags & RGEO_FACTORYFLAGS_SUPPORTS_Z_OR_M);
  }
  return result;
}


static VALUE method_geometry_collection_geometry_type(VALUE self)
{
  VALUE result = Qnil;
  RGeo_GeometryData* self_data = RGEO_GEOMETRY_DATA_PTR(self);
  if (self_data->geom) {
    result = RGEO_FACTORY_DATA_PTR(self_data->factory)->globals->feature_geometry_collection;
  }
  return result;
}


static VALUE method_geometry_collection_num_geometries(VALUE self)
{
  VALUE result = Qnil;
  RGeo_GeometryData* self_data = RGEO_GEOMETRY_DATA_PTR(self);
  const GEOSGeometry* self_geom = self_data->geom;
  if (self_geom) {
    result = INT2NUM(GEOSGetNumGeometries_r(self_data->geos_context, self_geom));
  }
  return result;
}


static VALUE impl_geometry_n(VALUE self, VALUE n, char allow_negatives)
{
  VALUE result = Qnil;
  RGeo_GeometryData* self_data = RGEO_GEOMETRY_DATA_PTR(self);
  const GEOSGeometry* self_geom = self_data->geom;
  if (self_geom) {
    VALUE klasses = self_data->klasses;
    int i = NUM2INT(n);
    if (allow_negatives || i >= 0) {
      GEOSContextHandle_t self_context = self_data->geos_context;
      int len = GEOSGetNumGeometries_r(self_context, self_geom);
      if (i < 0) {
        i += len;
      }
      if (i >= 0 && i < len) {
        result = rgeo_wrap_geos_geometry_clone(self_data->factory,
          GEOSGetGeometryN_r(self_context, self_geom, i),
          NIL_P(klasses) ? Qnil : rb_ary_entry(klasses, i));
      }
    }
  }
  return result;
}


static VALUE method_geometry_collection_geometry_n(VALUE self, VALUE n)
{
  impl_geometry_n(self, n, 0);
}


static VALUE method_geometry_collection_brackets(VALUE self, VALUE n)
{
  impl_geometry_n(self, n, 1);
}


static VALUE method_geometry_collection_each(VALUE self)
{
  RGeo_GeometryData* self_data = RGEO_GEOMETRY_DATA_PTR(self);
  const GEOSGeometry* self_geom = self_data->geom;
  if (self_geom) {
    GEOSContextHandle_t self_context = self_data->geos_context;
    int len = GEOSGetNumGeometries_r(self_context, self_geom);
    if (len > 0) {
      VALUE klasses = self_data->klasses;
      int i;
      for (i=0; i<len; ++i) {
        VALUE elem;
        const GEOSGeometry* elem_geom = GEOSGetGeometryN_r(self_context, self_geom, i);
        elem = rgeo_wrap_geos_geometry_clone(self_data->factory, elem_geom, NIL_P(klasses) ? Qnil : rb_ary_entry(klasses, i));
        if (!NIL_P(elem)) {
          rb_yield(elem);
        }
      }
    }
  }
  return self;
}


static VALUE method_multi_point_geometry_type(VALUE self)
{
  VALUE result = Qnil;
  RGeo_GeometryData* self_data = RGEO_GEOMETRY_DATA_PTR(self);
  if (self_data->geom) {
    result = RGEO_FACTORY_DATA_PTR(self_data->factory)->globals->feature_multi_point;
  }
  return result;
}


static VALUE method_multi_line_string_geometry_type(VALUE self)
{
  VALUE result = Qnil;
  RGeo_GeometryData* self_data = RGEO_GEOMETRY_DATA_PTR(self);
  if (self_data->geom) {
    result = RGEO_FACTORY_DATA_PTR(self_data->factory)->globals->feature_multi_line_string;
  }
  return result;
}


static VALUE method_multi_line_string_length(VALUE self)
{
  VALUE result = Qnil;
  RGeo_GeometryData* self_data = RGEO_GEOMETRY_DATA_PTR(self);
  const GEOSGeometry* self_geom = self_data->geom;
  if (self_geom) {
    double len;
    if (GEOSLength_r(self_data->geos_context, self_geom, &len)) {
      result = rb_float_new(len);
    }
  }
  return result;
}


static VALUE method_multi_line_string_is_closed(VALUE self)
{
  VALUE result = Qnil;
  RGeo_GeometryData* self_data = RGEO_GEOMETRY_DATA_PTR(self);
  const GEOSGeometry* self_geom = self_data->geom;
  if (self_geom) {
    GEOSContextHandle_t self_context = self_data->geos_context;
    result = Qtrue;
    int len = GEOSGetNumGeometries_r(self_context, self_geom);
    if (len > 0) {
      int i;
      for (i=0; i<len; ++i) {
        const GEOSGeometry* geom = GEOSGetGeometryN_r(self_context, self_geom, i);
        if (geom) {
          result = rgeo_is_geos_line_string_closed(self_context, self_geom);
          if (result != Qtrue) {
            break;
          }
        }
      }
    }
  }
  return result;
}


static VALUE method_multi_polygon_geometry_type(VALUE self)
{
  VALUE result = Qnil;
  RGeo_GeometryData* self_data = RGEO_GEOMETRY_DATA_PTR(self);
  if (self_data->geom) {
    result = RGEO_FACTORY_DATA_PTR(self_data->factory)->globals->feature_multi_polygon;
  }
  return result;
}


static VALUE method_multi_polygon_area(VALUE self)
{
  VALUE result = Qnil;
  RGeo_GeometryData* self_data = RGEO_GEOMETRY_DATA_PTR(self);
  const GEOSGeometry* self_geom = self_data->geom;
  if (self_geom) {
    double area;
    if (GEOSArea_r(self_data->geos_context, self_geom, &area)) {
      result = rb_float_new(area);
    }
  }
  return result;
}


static VALUE method_multi_polygon_centroid(VALUE self)
{
  VALUE result = Qnil;
  RGeo_GeometryData* self_data = RGEO_GEOMETRY_DATA_PTR(self);
  const GEOSGeometry* self_geom = self_data->geom;
  if (self_geom) {
    result = rgeo_wrap_geos_geometry(self_data->factory, GEOSGetCentroid_r(self_data->geos_context, self_geom), Qnil);
  }
  return result;
}


static VALUE method_multi_polygon_point_on_surface(VALUE self)
{
  VALUE result = Qnil;
  RGeo_GeometryData* self_data = RGEO_GEOMETRY_DATA_PTR(self);
  const GEOSGeometry* self_geom = self_data->geom;
  if (self_geom) {
    result = rgeo_wrap_geos_geometry(self_data->factory, GEOSPointOnSurface_r(self_data->geos_context, self_geom), Qnil);
  }
  return result;
}


static VALUE cmethod_geometry_collection_create(VALUE module, VALUE factory, VALUE array)
{
  return create_geometry_collection(module, GEOS_GEOMETRYCOLLECTION, factory, array);
}


static VALUE cmethod_multi_point_create(VALUE module, VALUE factory, VALUE array)
{
  return create_geometry_collection(module, GEOS_MULTIPOINT, factory, array);
}


static VALUE cmethod_multi_line_string_create(VALUE module, VALUE factory, VALUE array)
{
  return create_geometry_collection(module, GEOS_MULTILINESTRING, factory, array);
}


static VALUE cmethod_multi_polygon_create(VALUE module, VALUE factory, VALUE array)
{
  return create_geometry_collection(module, GEOS_MULTIPOLYGON, factory, array);
}


/**** INITIALIZATION FUNCTION ****/


void rgeo_init_geos_geometry_collection(RGeo_Globals* globals)
{
  // Create implementation classes
  VALUE geos_geometry_collection_class = rb_define_class_under(globals->geos_module, "GeometryCollectionImpl", globals->geos_geometry);
  globals->geos_geometry_collection = geos_geometry_collection_class;
  globals->feature_geometry_collection = rb_const_get_at(globals->feature_module, rb_intern("GeometryCollection"));
  VALUE geos_multi_point_class = rb_define_class_under(globals->geos_module, "MultiPointImpl", geos_geometry_collection_class);
  globals->geos_multi_point = geos_multi_point_class;
  globals->feature_multi_point = rb_const_get_at(globals->feature_module, rb_intern("MultiPoint"));
  VALUE geos_multi_line_string_class = rb_define_class_under(globals->geos_module, "MultiLineStringImpl", geos_geometry_collection_class);
  globals->geos_multi_line_string = geos_multi_line_string_class;
  globals->feature_multi_line_string = rb_const_get_at(globals->feature_module, rb_intern("MultiLineString"));
  VALUE geos_multi_polygon_class = rb_define_class_under(globals->geos_module, "MultiPolygonImpl", geos_geometry_collection_class);
  globals->geos_multi_polygon = geos_multi_polygon_class;
  globals->feature_multi_polygon = rb_const_get_at(globals->feature_module, rb_intern("MultiPolygon"));
  
  // Methods for GeometryCollectionImpl
  rb_define_module_function(geos_geometry_collection_class, "create", cmethod_geometry_collection_create, 2);
  rb_include_module(geos_geometry_collection_class, rb_define_module("Enumerable"));
  rb_define_method(geos_geometry_collection_class, "eql?", method_geometry_collection_eql, 1);
  rb_define_method(geos_geometry_collection_class, "geometry_type", method_geometry_collection_geometry_type, 0);
  rb_define_method(geos_geometry_collection_class, "num_geometries", method_geometry_collection_num_geometries, 0);
  rb_define_method(geos_geometry_collection_class, "size", method_geometry_collection_num_geometries, 0);
  rb_define_method(geos_geometry_collection_class, "geometry_n", method_geometry_collection_geometry_n, 1);
  rb_define_method(geos_geometry_collection_class, "[]", method_geometry_collection_brackets, 1);
  rb_define_method(geos_geometry_collection_class, "each", method_geometry_collection_each, 0);
  
  // Methods for MultiPointImpl
  rb_define_module_function(geos_multi_point_class, "create", cmethod_multi_point_create, 2);
  rb_define_method(geos_multi_point_class, "geometry_type", method_multi_point_geometry_type, 0);
  
  // Methods for MultiLineStringImpl
  rb_define_module_function(geos_multi_line_string_class, "create", cmethod_multi_line_string_create, 2);
  rb_define_method(geos_multi_line_string_class, "geometry_type", method_multi_line_string_geometry_type, 0);
  rb_define_method(geos_multi_line_string_class, "length", method_multi_line_string_length, 0);
  rb_define_method(geos_multi_line_string_class, "is_closed?", method_multi_line_string_is_closed, 0);
  
  // Methods for MultiPolygonImpl
  rb_define_module_function(geos_multi_polygon_class, "create", cmethod_multi_polygon_create, 2);
  rb_define_method(geos_multi_polygon_class, "geometry_type", method_multi_polygon_geometry_type, 0);
  rb_define_method(geos_multi_polygon_class, "area", method_multi_polygon_area, 0);
  rb_define_method(geos_multi_polygon_class, "centroid", method_multi_polygon_centroid, 0);
  rb_define_method(geos_multi_polygon_class, "point_on_surface", method_multi_polygon_point_on_surface, 0);
}


/**** OTHER PUBLIC FUNCTIONS ****/


VALUE rgeo_geos_geometry_collections_eql(GEOSContextHandle_t context, const GEOSGeometry* geom1, const GEOSGeometry* geom2, char check_z)
{
  VALUE result = Qnil;
  if (geom1 && geom2) {
    int len1 = GEOSGetNumGeometries_r(context, geom1);
    int len2 = GEOSGetNumGeometries_r(context, geom2);
    if (len1 >= 0 && len2 >= 0) {
      if (len1 == len2) {
        result = Qtrue;
        int i;
        for (i=0; i<len1; ++i) {
          const GEOSGeometry* sub_geom1 = GEOSGetGeometryN_r(context, geom1, i);
          const GEOSGeometry* sub_geom2 = GEOSGetGeometryN_r(context, geom2, i);
          if (sub_geom1 && sub_geom2) {
            int type1 = GEOSGeomTypeId_r(context, sub_geom1);
            int type2 = GEOSGeomTypeId_r(context, sub_geom2);
            if (type1 >= 0 && type2 >= 0) {
              if (type1 == type2) {
                switch (type1) {
                case GEOS_POINT:
                case GEOS_LINESTRING:
                case GEOS_LINEARRING:
                  result = rgeo_geos_coordseqs_eql(context, sub_geom1, sub_geom2, check_z);
                  break;
                case GEOS_POLYGON:
                  result = rgeo_geos_polygons_eql(context, sub_geom1, sub_geom2, check_z);
                  break;
                case GEOS_GEOMETRYCOLLECTION:
                case GEOS_MULTIPOINT:
                case GEOS_MULTILINESTRING:
                case GEOS_MULTIPOLYGON:
                  result = rgeo_geos_geometry_collections_eql(context, sub_geom1, sub_geom2, check_z);
                  break;
                default:
                  result = Qnil;
                  break;
                }
                if (!RTEST(result)) {
                  break;
                }
              }
              else {
                result = Qfalse;
                break;
              }
            }
            else {
              result = Qnil;
              break;
            }
          }
          else {
            result = Qnil;
            break;
          }
        }
      }
      else {
        result = Qfalse;
      }
    }
  }
  return result;
}


RGEO_END_C

#endif
