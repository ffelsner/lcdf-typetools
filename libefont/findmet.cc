#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#ifdef __GNUG__
# pragma implementation "findmet.hh"
#endif
#include "findmet.hh"
#include "afmparse.hh"
#include "afm.hh"
#include "amfm.hh"
#include "psres.hh"
#include <string.h>
#include <stdlib.h>


inline
MetricsFinder::MetricsFinder()
  : _next(0), _prev(0)
{
}

MetricsFinder::~MetricsFinder()
{
  if (_next) _next->_prev = _prev;
  if (_prev) _prev->_next = _next;
}

void
MetricsFinder::append(MetricsFinder *new_finder)
{
  if (_next)
    _next->append(new_finder);
  else {
    assert(!new_finder->_prev);
    new_finder->_prev = this;
    _next = new_finder;
  }
}

Metrics *
MetricsFinder::find_metrics(PermString name, ErrorHandler *errh)
{
  MetricsFinder *f = this;
  while (f) {
    Metrics *m = f->find_metrics_x(name, this, errh);
    if (m) return m;
    f = f->_next;
  }
  return 0;
}

AmfmMetrics *
MetricsFinder::find_amfm(PermString name, ErrorHandler *errh)
{
  MetricsFinder *f = this;
  while (f) {
    AmfmMetrics *m = f->find_amfm_x(name, this, errh);
    if (m) return m;
    f = f->_next;
  }
  return 0;
}

void
MetricsFinder::record(Metrics *m)
{
  record(m, m->font_name());
}

void
MetricsFinder::record(Metrics *m, PermString name)
{
  if (_next) _next->record(m, name);
}

void
MetricsFinder::record(AmfmMetrics *amfm)
{
  if (_next) _next->record(amfm);
}

Metrics *
MetricsFinder::find_metrics_x(PermString, MetricsFinder *, ErrorHandler *)
{
  return 0;
}

AmfmMetrics *
MetricsFinder::find_amfm_x(PermString, MetricsFinder *, ErrorHandler *)
{
  return 0;
}

Metrics *
MetricsFinder::try_metrics_file(const Filename &fn, MetricsFinder *finder,
				ErrorHandler *errh)
{
  if (fn.readable()) {
    Metrics *afm = AfmReader::read(fn, errh);
    if (afm) finder->record(afm);
    return afm;
  } else
    return 0;
}

AmfmMetrics *
MetricsFinder::try_amfm_file(const Filename &fn, MetricsFinder *finder,
			     ErrorHandler *errh)
{
  if (fn.readable()) {
    AmfmMetrics *amfm = AmfmReader::read(fn, finder, errh);
    if (amfm) finder->record(amfm);
    return amfm;
  } else
    return 0;
}


/*****
 * CacheMetricsFinder
 **/

CacheMetricsFinder::CacheMetricsFinder()
  : _metrics_map(-1), _amfm_map(-1)
{
}

CacheMetricsFinder::~CacheMetricsFinder()
{
  clear();
}

Metrics *
CacheMetricsFinder::find_metrics_x(PermString name, MetricsFinder *,
				   ErrorHandler *)
{
  int index = _metrics_map[name];
  return index >= 0 ? _metrics[index] : 0;
}

AmfmMetrics *
CacheMetricsFinder::find_amfm_x(PermString name, MetricsFinder *,
				ErrorHandler*)
{
  int index = _amfm_map[name];
  return index >= 0 ? _amfm[index] : 0;
}


void
CacheMetricsFinder::record(Metrics *m, PermString name)
{
  int index = _metrics.append(m);
  _metrics_map.insert(name, index);
  m->use();
  MetricsFinder::record(m, name);
}

void
CacheMetricsFinder::record(AmfmMetrics *amfm)
{
  int index = _amfm.append(amfm);
  _amfm_map.insert(amfm->font_name(), index);
  amfm->use();
  MetricsFinder::record(amfm);
}

void
CacheMetricsFinder::clear()
{
  for (int i = 0; i < _metrics.count(); i++)
    _metrics[i]->unuse();
  for (int i = 0; i < _amfm.count(); i++)
    _amfm[i]->unuse();
  _metrics.clear();
  _amfm.clear();
  _metrics_map.clear();
  _amfm_map.clear();
}


/*****
 * InstanceMetricsFinder
 **/

InstanceMetricsFinder::InstanceMetricsFinder()
{
}

Metrics *
InstanceMetricsFinder::find_metrics_instance(PermString name,
				MetricsFinder *finder, ErrorHandler *errh)
{
  char *underscore = strchr(name, '_');
  PermString amfm_name =
    PermString(name.cc(), underscore - name.cc());
  
  AmfmMetrics *amfm = finder->find_amfm(amfm_name, errh);
  if (!amfm) return 0;
  
  Type1MMSpace *mmspace = amfm->mmspace();
  Vector<double> design = mmspace->default_design_vector();
  int i = 0;
  while (underscore[0] == '_' && underscore[1]) {
    double x = strtod(underscore + 1, &underscore);
    mmspace->set_design(design, i, x, errh);
    i++;
  }
  
  Vector<double> weight;
  if (!mmspace->design_to_weight(design, weight, errh))
    return 0;
  Metrics *new_afm = amfm->interpolate(design, weight, errh);
  if (new_afm) {
    finder->record(new_afm);
    // What if the dimensions changed because the user specified out-of-range
    // dimens? We don't want to reinterpolate each time, so record the new
    // AFM under that name as well.
    if (new_afm->font_name() != name)
      finder->record(new_afm, name);
  }
  return new_afm;
}

Metrics *
InstanceMetricsFinder::find_metrics_x(PermString name, MetricsFinder *finder,
				      ErrorHandler *errh)
{
  if (strchr(name, '_'))
    return find_metrics_instance(name, finder, errh);
  else
    return 0;
}


/*****
 * PsresMetricsFinder
 **/

PsresMetricsFinder::PsresMetricsFinder(PsresDatabase *psres)
  : _psres(psres)
{
}

Metrics *
PsresMetricsFinder::find_metrics_x(PermString name, MetricsFinder *finder,
				   ErrorHandler *errh)
{
  return try_metrics_file
    (_psres->filename_value("FontAFM", name), finder, errh);
}

AmfmMetrics *
PsresMetricsFinder::find_amfm_x(PermString name, MetricsFinder *finder,
				ErrorHandler *errh)
{
  return try_amfm_file
    (_psres->filename_value("FontAMFM", name), finder, errh);
}


/*****
 * DirectoryMetricsFinder
 **/

DirectoryMetricsFinder::DirectoryMetricsFinder(PermString d)
  : _directory(d)
{
}

Metrics *
DirectoryMetricsFinder::find_metrics_x(PermString name, MetricsFinder *finder,
				       ErrorHandler *errh)
{
  Metrics *afm = try_metrics_file
    (Filename(_directory, permcat(name, ".afm")), finder, errh);
  if (!afm)
    afm = try_metrics_file
      (Filename(_directory, permcat(name, ".AFM")), finder, errh);
  return afm;
}

AmfmMetrics *
DirectoryMetricsFinder::find_amfm_x(PermString name, MetricsFinder *finder,
				    ErrorHandler *errh)
{
  AmfmMetrics *amfm = try_amfm_file
    (Filename(_directory, permcat(name, ".amfm")), finder, errh);
  if (!amfm)
    amfm = try_amfm_file
      (Filename(_directory, permcat(name, ".AMFM")), finder, errh);
  return amfm;
}