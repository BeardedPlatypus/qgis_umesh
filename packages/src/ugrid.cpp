#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>

using namespace std;

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(WIN32) || defined(WIN64)
#  include <direct.h>
#  include <windows.h>
#else
#  include <unistd.h>
#endif

#if defined(WIN32) || defined(WIN64)
#  define strdup _strdup
#  define strcat strcat_s
#  define getcwd _getcwd
#  define FILE_NAME_MAX _MAX_PATH
#else
#  define FILE_NAME_MAX FILENAME_MAX
#endif

#include "ugrid.h"
#include "netcdf.h"

//------------------------------------------------------------------------------
#ifdef NATIVE_C
    UGRID::UGRID(char * filename)
#else
    UGRID::UGRID(QFileInfo filename, QProgressBar * pgBar)
#endif
{
#ifdef NATIVE_C
    this->fname = strdup(filename);  // filename without path, just the name
    this->ugrid_file_name = strdup(filename);  // filename with complete path
#else
    this->fname = filename;  // filename without path, just the name
    this->ugrid_file_name = filename.absoluteFilePath();  // filename with complete path
    m_pgBar = pgBar;
#endif
    m_nr_mesh_contacts = 0;
    _two = 2;
}
//------------------------------------------------------------------------------
UGRID::~UGRID()
{
#ifdef NATIVE_C
    free(this->fname);
    free(this->ugrid_file_name);
#endif    
    (void)nc_close(this->ncid);

    // free the memory
    for (long i = 0; i < global_attributes->count; i++)
    {
        //delete global_attributes->attribute[i];
    }
    free(global_attributes->attribute);
    delete global_attributes;
    // free DataValueProvider2D3D
    for (int i = 0; i < m_nr_mesh_var; i++)
    {
        free(mesh_vars->variable[m_nr_mesh_var - 1]->data_2d.m_arrayPtr);
        mesh_vars->variable[m_nr_mesh_var - 1]->data_2d.m_arrayPtr = nullptr;
        free(mesh_vars->variable[m_nr_mesh_var - 1]->data_3d.m_arrayPtr);
        mesh_vars->variable[m_nr_mesh_var - 1]->data_3d.m_arrayPtr = nullptr;
    }
}
//------------------------------------------------------------------------------
long UGRID::read()
{
    int status = -1;
#ifdef NATIVE_C
    status = nc_open(this->ugrid_file_name, NC_NOWRITE, &this->ncid);
    if (status != NC_NOERR)
    {
        fprintf(stderr, "UGRID::read()\n\tFailed to open file: %s\n", this->ugrid_file_name);
        return status;
    }
    fprintf(stderr, "UGRID::read()\n\tOpened: %s\n", this->ugrid_file_name);

#else
    char * ug_fname = strdup(this->fname.absoluteFilePath().toUtf8());
    status = nc_open(ug_fname, NC_NOWRITE, &this->ncid);
    if (status != NC_NOERR)
    {
        QMessageBox::critical(0, QString("Error"), QString("UGRID::read()\n\tFailed to open file: %1").arg(ug_fname));
        return status;
    }
    free(ug_fname);
    ug_fname = nullptr;
#endif
    status = this->read_global_attributes();
    status = this->read_mesh();
    status = this->read_times();
    status = this->read_variables();

    return status;
}
//------------------------------------------------------------------------------
long UGRID::read_global_attributes()
{
    long status;
    int ndims, nvars, natts, nunlimited;
    nc_type att_type;
    size_t att_length;

#ifdef NATIVE_C
    fprintf(stderr, "UGRID::read_global_attributes()\n");
#endif    

    char * att_name_c = (char *)malloc(sizeof(char) * (NC_MAX_NAME + 1));
    att_name_c[0] = '\0';

    status = nc_inq(this->ncid, &ndims, &nvars, &natts, &nunlimited);

    this->global_attributes = (struct _global_attributes *)malloc(sizeof(struct _global_attributes));
    this->global_attributes->count = natts;
    this->global_attributes->attribute = (struct _global_attribute **)malloc(sizeof(struct _global_attribute *) * natts);
    for (long i = 0; i < natts; i++)
    {
        this->global_attributes->attribute[i] = new _global_attribute;
    }

    for (long i = 0; i < natts; i++)
    {
        status = nc_inq_attname(this->ncid, NC_GLOBAL, i, att_name_c);
        status = nc_inq_att(this->ncid, NC_GLOBAL, att_name_c, &att_type, &att_length);
        this->global_attributes->attribute[i]->name = string(strdup(att_name_c));
        this->global_attributes->attribute[i]->type = att_type;
        this->global_attributes->attribute[i]->length = att_length;
        if (att_type == NC_CHAR)
        {
            char * att_value_c = (char *)malloc(sizeof(char) * (att_length + 1));
            att_value_c[0] = '\0';
            status = nc_get_att_text(this->ncid, NC_GLOBAL, att_name_c, att_value_c);
            att_value_c[att_length] = '\0';
            this->global_attributes->attribute[i]->cvalue = string(strdup(att_value_c));
            free(att_value_c);
            att_value_c = nullptr;
        }
        else
        {
#ifdef NATIVE_C
            fprintf(stderr, "\tAttribute nc_type: %d\n", att_type);
#endif    
        }
    }
    free(att_name_c);
    att_name_c = nullptr;

    return status;
}
//------------------------------------------------------------------------------
struct _global_attributes * UGRID::get_global_attributes()
{
#ifdef NATIVE_C
    fprintf(stderr, "UGRID::get_global_attributes()\n");
#endif    
    return this->global_attributes;
}
#ifdef NATIVE_C
//------------------------------------------------------------------------------
char * UGRID::get_filename()
{
    return this->ugrid_file_name;
}
#else
//------------------------------------------------------------------------------
QFileInfo UGRID::get_filename()
{
    return this->ugrid_file_name;
}
#endif
//------------------------------------------------------------------------------
long UGRID::read_mesh()
{
    int ndims, nvars, natts, nunlimited;
    int unlimid;
    string cf_role;
    string grid_mapping_name;
    string std_name;
    vector<string> tmp_dim_names;
    int status = 1;
    size_t length;

#ifdef NATIVE_C
    fprintf(stderr, "UGRID::read_mesh()\n");
#else
    m_pgBar->setValue(15);
#endif    

    char * var_name_c = (char *)malloc(sizeof(char) * (NC_MAX_NAME + 1));
    mapping = new _mapping();
    set_grid_mapping_epsg(0, "EPSG:0");

    status = nc_inq(this->ncid, &ndims, &nvars, &natts, &nunlimited);
    m_dimids = (size_t *)malloc(sizeof(size_t) * ndims);
    for (int i = 0; i < ndims; i++)
    {
        status = nc_inq_dimlen(this->ncid, i, &m_dimids[i]);
        status = nc_inq_dimname(this->ncid, i, var_name_c);
        m_dim_names.push_back(string(var_name_c));
        m_map_dim[string(var_name_c)] = m_dimids[i];
    }
    status = nc_inq_unlimdim(this->ncid, &unlimid);
    for (int i_var = 0; i_var < nvars; i_var++)
    {
        nc_type nc_type;
        nc_inq_varndims(this->ncid, i_var, &ndims);
        int * var_dimids = NULL;
        if (ndims > 0) {
            var_dimids = (int *)malloc(sizeof(int) * ndims);
        }
        status = nc_inq_var(this->ncid, i_var, var_name_c, &nc_type, &ndims, var_dimids, &natts);
        string var_name(var_name_c);
#ifdef NATIVE_C
        fprintf(stderr, "UGRID::read_mesh()\n\tVariable name: %d - %s\n", i_var + 1, var_name.c_str());
#else
        m_pgBar->setValue(20 + (i_var + 1) / nvars * (500 - 20));
        //QMessageBox::warning(0, QString("Warning"), QString("UGRID::get_grid_mapping()\nProgress: (%1, %2) %3").arg(i_var + 1).arg(nvars).arg(var_name));
#endif
        length = 0;
        status = get_attribute(this->ncid, i_var, "cf_role", &cf_role);
        if (status == NC_NOERR)
        {
            status = read_variables_with_cf_role(i_var, var_name, cf_role, ndims, var_dimids);
        }

        status = get_attribute(this->ncid, i_var, "standard_name", &std_name);
        if (std_name == "ocean_sigma_coordinate")
        {
            // this grid contains sigma layers, find sigma dimension name
            tmp_dim_names = get_dimension_names(this->ncid, var_name);
            for (int i = 0; i < tmp_dim_names.size(); i++)
            {
                if (tmp_dim_names[i].find("nterface") != string::npos)  // HACK: dimension name should have the sub-string 'interface' or 'Interface'
                {
                    m_map_dim_name["zs_dim_interface"] = tmp_dim_names[i];
                    m_map_dim_name["zs_name_interface"] = var_name;
                }
                else
                {
                    m_map_dim_name["zs_dim_layer"] = tmp_dim_names[i];
                    m_map_dim_name["zs_name_layer"] = var_name;
                }
            }
        }
        if (std_name == "altitude")
        {
            // this grid contains z- layers, find z-dimension name
            tmp_dim_names = get_dimension_names(this->ncid, var_name);
            for (int i = 0; i < tmp_dim_names.size(); i++)
            {
                if (tmp_dim_names[i].find("nterface") != string::npos)
                {
                    m_map_dim_name["zs_dim_interface"] = tmp_dim_names[i];
                    m_map_dim_name["zs_name_interface"] = var_name;
                }
                else if (tmp_dim_names[i].find("Layer") != string::npos)
                {
                    m_map_dim_name["zs_dim_layer"] = tmp_dim_names[i];
                    m_map_dim_name["zs_name_layer"] = var_name;
                }
            }
        }

        status = get_attribute(this->ncid, i_var, "grid_mapping_name", &grid_mapping_name);
        if (status == NC_NOERR)
        {
            status = read_grid_mapping(i_var, var_name, grid_mapping_name);
        }
        free(var_dimids);
        var_dimids = nullptr;
    }

#ifndef NATIVE_C
    m_pgBar->setValue(600);
#endif
    status = create_mesh1d_nodes(mesh1d, ntw_edges, ntw_geom);

    if (mesh_contact != NULL)
    {
        // create the mesh contact edges
        // llog first for the meshes (ie the var_names) and contact points
        string mesh_a;
        string mesh_b;
        string location_a;
        string location_b;

        int i = 0;
        for (int i_var = 0; i_var < nvars; i_var++)
        {
            status = nc_inq_varname(this->ncid, i_var, var_name_c);
            string var_name(var_name_c);
            if (var_name == mesh_contact->mesh_a)
            {
                mesh_a = mesh_contact->mesh_a;
                location_a = mesh_contact->location_a;
                i++;
            }
            if (var_name == mesh_contact->mesh_b)
            {
                mesh_b = mesh_contact->mesh_b;
                location_b = mesh_contact->location_b;
                i++;
            }
            if (i == 2)
            {
                break;
            }
        }
        // looking for mesh_a
        mesh_contact->node[0]->x = vector<double>(mesh_contact->node[m_nr_mesh_contacts - 1]->count);
        mesh_contact->node[0]->y = vector<double>(mesh_contact->node[m_nr_mesh_contacts - 1]->count);
        if (location_a == "node")
        {
            for (int j = 0; j < mesh_contact->edge[0]->count; j++)
            {
                int p1 = mesh_contact->edge[0]->edge_nodes[j][0];
                mesh_contact->node[0]->x[2 * j] = mesh1d->node[0]->x[p1];
                mesh_contact->node[0]->y[2 * j] = mesh1d->node[0]->y[p1];
                mesh_contact->edge[0]->edge_nodes[j][0] = 2 * j;
            }
        }
        // looking for mesh_b
        if (location_b == "face")
        {
            for (int j = 0; j < mesh_contact->edge[0]->count; j++)
            {
                int p1 = mesh_contact->edge[0]->edge_nodes[j][1];
                mesh_contact->node[0]->x[2 * j + 1] = mesh2d->face[0]->x[p1];
                mesh_contact->node[0]->y[2 * j + 1] = mesh2d->face[0]->y[p1];
                mesh_contact->edge[0]->edge_nodes[j][1] = 2 * j + 1;
            }
        }
        size_t length = mesh_contact->node[0]->name.size();
        length += mesh_contact->node[0]->long_name.size();
        length += mesh_contact->edge[0]->name.size();
        length += mesh_contact->edge[0]->long_name.size();
    }
    //status = create_mesh_contacts(mesh_a, location_a, mesh_b, location_b);

#ifndef NATIVE_C
    m_pgBar->setValue(700);
#endif
    free(var_name_c);
    var_name_c = nullptr;

    return status;
}
//------------------------------------------------------------------------------
long UGRID::set_grid_mapping_epsg(long epsg, string epsg_code)
{
    long status = 0;
    mapping->epsg = epsg;
    mapping->epsg_code = epsg_code;
    return status;
}
//------------------------------------------------------------------------------
long UGRID::read_times()
{
    // Look for variable with unit like: "seconds since 2017-02-25 15:26:00"
    int ndims, nvars, natts, nunlimited;
    int status;
    char * var_name_c;
    char * units_c;
    size_t length;
    int dimids;
    int nr_time_series = 0;
    double dt;

    QStringList date_time;
    char * time_var_name;
    QDateTime * RefDate;
    struct _time_series t_series;
    time_series.push_back(t_series);

#ifndef NATIVE_C
    m_pgBar->setValue(700);
#endif
    var_name_c = (char *)malloc(sizeof(char) * (NC_MAX_NAME + 1));
    var_name_c[0] = '\0';
    status = nc_inq(this->ncid, &ndims, &nvars, &natts, &nunlimited);
    for (long i_var = 0; i_var < nvars; i_var++)
    {
        length = -1;
        status = nc_inq_attlen(this->ncid, i_var, "units", &length);
        if (status == NC_NOERR)
        {
            units_c = (char *)malloc(sizeof(char) * (length + 1));
            units_c[length] = '\0';
            status = nc_get_att(this->ncid, i_var, "units", units_c);
            QString units = QString(units_c).replace("T", " ");  // "seconds since 1970-01-01T00:00:00" changed into "seconds since 1970-01-01 00:00:00"
            date_time = units.split(" ");
            if (date_time.count() >= 2)
            {
                if (!strcmp("since", date_time.at(1).toUtf8()))
                {
                    // now it is the time variable, can only be detected by the "seconds since 1970-01-01T00:00:00" character string
                    // retrieve the long_name, standard_name -> var_name for the xaxis label
                    length = -1;
                    status = nc_inq_var(this->ncid, i_var, var_name_c, NULL, &ndims, &dimids, &natts);
                    status = nc_inq_dimname(this->ncid, dimids, var_name_c);
                    time_series[0].dim_name = new QString(var_name_c);
                    status = nc_inq_attlen(this->ncid, i_var, "long_name", &length);
                    if (status == NC_NOERR)
                    {
                        char * c_label = (char *)malloc(sizeof(char) * (length + 1));
                        c_label[length] = '\0';
                        status = nc_get_att(this->ncid, i_var, "long_name", c_label);
                        time_series[0].long_name = new QString(c_label);
                        free(c_label); 
                        c_label = nullptr;
                    }
                    else
                    {
                        time_series[0].long_name = new QString(var_name_c);
                    }
                    //status = get_dimension_names(this->ncid, var_name_c, &time_series[0].dim_name);

                    // retrieve the time series
                    nr_time_series += 1;
                    if (nr_time_series > 1)
                    {
#if defined NATIVE_C
#else
                        QMessageBox::warning(NULL, QObject::tr("Warning"), QObject::tr("Support of just one time series, only the first one will be supported.\nPlease mail to jan.mooiman@deltares.nl"));
#endif
                        continue;
                    }

                    status = nc_inq_dimlen(this->ncid, dimids, &time_series[0].nr_times);
                    time_var_name = strdup(var_name_c);
                    m_map_dim[time_var_name] = time_series[0].nr_times;
                    m_map_dim_name["time"] = time_var_name;

                    qdt_times.reserve((int)time_series[0].nr_times);  // HACK typecast
                    // ex. date_time = "seconds since 2017-02-25 15:26:00"   year, yr, day, d, hour, hr, h, minute, min, second, sec, s and all plural forms
                    time_series[0].unit = new QString(date_time.at(0));

                    QDate date = QDate::fromString(date_time.at(2), "yyyy-MM-dd");
                    QTime time = QTime::fromString(date_time.at(3), "hh:mm:ss");
                    RefDate = new QDateTime(date, time, Qt::UTC);

#if defined(DEBUG)
                    QString janm1 = date.toString("yyyy-MM-dd");
                    QString janm2 = time.toString();
                    QString janm3 = RefDate->toString("yyyy-MM-dd hh:mm:ss.zzz");
#endif
                    time_series[0].times.reserve(time_series[0].nr_times);
                    status = nc_get_var_double(this->ncid, i_var, time_series[0].times.data());
                    if (time_series[0].nr_times >= 2)
                    {
                        dt = time_series[0].times[1] - time_series[0].times[0];
                    }

                    if (time_series[0].unit->contains("sec") ||
                        time_series[0].unit->trimmed() == "s")  // seconds, second, sec, s
                    {
                        time_series[0].unit = new QString("sec");
                    }
                    else if (time_series[0].unit->contains("min"))  // minutes, minute, min
                    {
                        time_series[0].unit = new QString("min");
                    }
                    else if (time_series[0].unit->contains("h"))  // hours, hour, hrs, hr, h
                    {
                        time_series[0].unit = new QString("hour");
                    }
                    else if (time_series[0].unit->contains("d"))  // days, day, d
                    {
                        time_series[0].unit = new QString("day");
                    }
                    for (int j = 0; j < time_series[0].nr_times; j++)
                    {
                        if (time_series[0].unit->contains("sec") ||
                            time_series[0].unit->trimmed() == "s")  // seconds, second, sec, s
                        {
                            //QMessageBox::warning(NULL, "Warning", QString("RefDate: %1").arg(RefDate->addSecs(time_series[0].times[j]).toString()));
                            if (dt < 1.0)
                            {
                                qdt_times.append(RefDate->addMSecs(1000.*time_series[0].times[j]));  // milli seconds as smallest time unit
                            }
                            else
                            {
                                qdt_times.append(RefDate->addSecs(time_series[0].times[j]));  // seconds as smallest time unit
                            }
                        }
                        else if (time_series[0].unit->contains("min"))  // minutes, minute, min
                        {
                            qdt_times.append(RefDate->addSecs(time_series[0].times[j] * 60.0));
                        }
                        else if (time_series[0].unit->contains("h"))  // hours, hour, hrs, hr, h
                        {
                            qdt_times.append(RefDate->addSecs(time_series[0].times[j] * 3600.0));
                        }
                        else if (time_series[0].unit->contains("d"))  // days, day, d
                        {
                            qdt_times.append(RefDate->addSecs(time_series[0].times[j] * 24.0 * 3600.0));
                        }

#if defined (DEBUG)
                        QString janm;
                        if (dt < 1.0)
                        {
                            janm = qdt_times[j].toString("yyyy-MM-dd hh:mm:ss.zzz");
                        }
                        else
                        {
                            janm = qdt_times[j].toString("yyyy-MM-dd hh:mm:ss");
                        }
#endif
                    }
                    break;
                }
            }
            free(units_c);
            units_c = nullptr;
        }
    }
    free(var_name_c); 
    var_name_c = nullptr;
#ifndef NATIVE_C
    m_pgBar->setValue(800);
#endif

    return (long)status;
}
//------------------------------------------------------------------------------
long UGRID::get_count_times()
{
    size_t nr = time_series[0].nr_times;
    return (long) nr;
}
//------------------------------------------------------------------------------
vector<double> UGRID::get_times()
{
    return time_series[0].times;
}
//------------------------------------------------------------------------------
QVector<QDateTime> UGRID::get_qdt_times()  // qdt: Qt Date Time
{
    return qdt_times;
}
//------------------------------------------------------------------------------
long UGRID::read_variables()
{
    // read the attributes of the variable  and the dimensions
#ifdef NATIVE_C
    fprintf(stderr, "UGRID::read_variables()\n");
#endif    
    int ndims, nvars, natts, nunlimited;
    int status;
    struct _variable * var;
    var = NULL;
#ifndef NATIVE_C
    m_pgBar->setValue(800);
#endif
    m_nr_mesh_var = 0;

    string tmp_string;
    char * var_name_c = (char *) malloc(sizeof(char) * (NC_MAX_NAME + 1));
    status = nc_inq(this->ncid, &ndims, &nvars, &natts, &nunlimited);

    for (long i_var = 0; i_var < nvars; i_var++)
    {
        nc_type nc_type;
        nc_inq_varndims(this->ncid, i_var, &ndims);
        int * var_dimids = NULL;
        if (ndims > 0) {
            var_dimids = (int *)malloc(sizeof(int) * ndims);
        }
        status = nc_inq_var(this->ncid, i_var, var_name_c, &nc_type, &ndims, var_dimids, &natts);
        string var_name(var_name_c);
        status = get_attribute(this->ncid, i_var, "mesh", &tmp_string);  // statement, to detect if this is a variable on a mesh
        if (status == NC_NOERR) { status = get_attribute(this->ncid, i_var, "location", &tmp_string); } // each variable does have a location
        if (status == NC_NOERR)
        {
            // This variable is defined on a mesh and has a dimension
            m_nr_mesh_var += 1;
            if (m_nr_mesh_var == 1)
            {
                mesh_vars = new _mesh_variable();
                mesh_vars->variable = (struct _variable **)malloc(sizeof(struct _variable *));
            }
            else
            {
                mesh_vars->variable = (struct _variable **)realloc(mesh_vars->variable, sizeof(struct _variable *) * m_nr_mesh_var);
            }
            mesh_vars->variable[m_nr_mesh_var - 1] = new _variable();
            mesh_vars->nr_vars = m_nr_mesh_var;

            mesh_vars->variable[m_nr_mesh_var - 1]->var_name = var_name;
            mesh_vars->variable[m_nr_mesh_var - 1]->nc_type = nc_type;
            mesh_vars->variable[m_nr_mesh_var - 1]->read = false;
            status = get_attribute(this->ncid, i_var, "location", &mesh_vars->variable[m_nr_mesh_var - 1]->location);
            status = get_attribute(this->ncid, i_var, "mesh", &mesh_vars->variable[m_nr_mesh_var - 1]->mesh);
            status = get_attribute(this->ncid, i_var, "coordinates", &mesh_vars->variable[m_nr_mesh_var - 1]->coordinates);
            status = get_attribute(this->ncid, i_var, "cell_methods", &mesh_vars->variable[m_nr_mesh_var - 1]->cell_methods);
            status = get_attribute(this->ncid, i_var, "standard_name", &mesh_vars->variable[m_nr_mesh_var - 1]->standard_name);
            status = get_attribute(this->ncid, i_var, "long_name", &mesh_vars->variable[m_nr_mesh_var - 1]->long_name);
            if (status != NC_NOERR || mesh_vars->variable[m_nr_mesh_var - 1]->long_name.size() == 0)
            {
                mesh_vars->variable[m_nr_mesh_var - 1]->long_name = mesh_vars->variable[m_nr_mesh_var - 1]->standard_name;
            }
            status = get_attribute(this->ncid, i_var, "units", &mesh_vars->variable[m_nr_mesh_var - 1]->units);
            status = get_attribute(this->ncid, i_var, "grid_mapping", &mesh_vars->variable[m_nr_mesh_var - 1]->grid_mapping);
            status = get_attribute(this->ncid, i_var, "_FillValue", &mesh_vars->variable[m_nr_mesh_var - 1]->fill_value);
            if (status != NC_NOERR)
            {
                mesh_vars->variable[m_nr_mesh_var - 1]->fill_value = numeric_limits<double>::quiet_NaN();
            }
            status = get_attribute(this->ncid, i_var, "comment", &mesh_vars->variable[m_nr_mesh_var - 1]->comment);

            mesh_vars->variable[m_nr_mesh_var - 1]->time_series = false;
            for (int j = 0; j < ndims; j++)
            {
                status = nc_inq_dimname(this->ncid, var_dimids[j], var_name_c);
                
                mesh_vars->variable[m_nr_mesh_var - 1]->dims.push_back((long)m_dimids[var_dimids[j]]);  // HACK typecast: size_t -> long
                if (time_series[0].nr_times != 0 && QString::fromStdString(m_dim_names[var_dimids[j]]) == time_series[0].dim_name)
                {
                    mesh_vars->variable[m_nr_mesh_var - 1]->time_series = true;
                }
                mesh_vars->variable[m_nr_mesh_var - 1]->dim_names.push_back(m_dim_names[var_dimids[j]]);
            }

            int topo_dim;
            int mesh_id;
            status = nc_inq_varid(this->ncid, mesh_vars->variable[m_nr_mesh_var - 1]->mesh.c_str(), &mesh_id);
            status = get_attribute(this->ncid, mesh_id, "topology_dimension", &topo_dim);
            mesh_vars->variable[m_nr_mesh_var - 1]->topology_dimension = topo_dim;
            if (mesh_vars->variable[m_nr_mesh_var - 1]->dims.size() == 3)
            {
                bool contains_time_dimension = false;
                for (int i = 0; i < mesh_vars->variable[m_nr_mesh_var - 1]->dims.size(); i++)
                {
                    // check if one of the dimension is the time dimension
                    if (QString::fromStdString(mesh_vars->variable[m_nr_mesh_var - 1]->dim_names[i]) == time_series[0].dim_name)
                    {
                        contains_time_dimension = true;
                        break;
                    }
                }
                if (contains_time_dimension)
                {
                    for (int i = 0; i < mesh_vars->variable[m_nr_mesh_var - 1]->dims.size(); i++)
                    {
                        if (mesh_vars->variable[m_nr_mesh_var - 1]->dim_names[i] == m_map_dim_name["zs_dim_layer"])
                        {
                            int nr_lay = m_map_dim[m_map_dim_name["zs_dim_layer"]];
                            mesh_vars->variable[m_nr_mesh_var - 1]->nr_layers = nr_lay;

                            string name = m_map_dim_name["zs_name_layer"];
                            int i_var;
                            status = nc_inq_varid(ncid, name.c_str(), &i_var);
                            double * values_c = (double *)malloc(sizeof(double) * nr_lay);
                            status = nc_get_var_double(this->ncid, i_var, values_c);
                            vector<double> values;
                            values.reserve(nr_lay);
                            for (int j = 0; j < nr_lay; j++)
                            {
                                values.push_back(*(values_c + j));
                            }
                            mesh_vars->variable[m_nr_mesh_var - 1]->layer_center = values;
                        }
                        else if (mesh_vars->variable[m_nr_mesh_var - 1]->dim_names[i] == m_map_dim_name["zs_dim_interface"])
                        {
                            int nr_lay = m_map_dim[m_map_dim_name["zs_dim_interface"]];
                            mesh_vars->variable[m_nr_mesh_var - 1]->nr_layers = nr_lay;

                            string name = m_map_dim_name["zs_name_interface"];
                            int i_var;
                            status = nc_inq_varid(ncid, name.c_str(), &i_var);
                            double * values_c = (double *)malloc(sizeof(double) * nr_lay);
                            status = nc_get_var_double(this->ncid, i_var, values_c);
                            vector<double> values;
                            values.reserve(nr_lay);
                            for (int j = 0; j < nr_lay; j++)
                            {
                                values.push_back(*(values_c + j));
                            }
                            mesh_vars->variable[m_nr_mesh_var - 1]->layer_center = values;
                        } 
                        else if (mesh_vars->variable[m_nr_mesh_var - 1]->dim_names[i] == "nSedTot")  // todo: hard coded sediment dimension
                        {
                            // because it is not a 3D dimesional data set no layer information, just time, space and constituent:
                            // generate new variables for each dimesnion which s not the time and space dimension
                            // in this example it nSedTot extra variables
                            // - time
                            // - sediment dimension
                            // - space dimension

                            int jmax = m_map_dim["nSedTot"];
                            for (int j = 0; j < jmax; j++)
                            {
                                vector<string> name(2, "");
                                vector<int> dims(2, 0);
                                for (int ii = 0; ii < 3; ii++)
                                {
                                    if (QString::fromStdString(mesh_vars->variable[m_nr_mesh_var - 1]->dim_names[ii]) == time_series[0].dim_name)
                                    {
                                        name[0] = "time";
                                        dims[0] = m_map_dim[name[0]];
                                    }
                                    else if (QString::fromStdString(mesh_vars->variable[m_nr_mesh_var - 1]->dim_names[ii]) != "nSedTot")
                                    {
                                        name[1] = mesh_vars->variable[m_nr_mesh_var - 1]->dim_names[ii];
                                        dims[1] = m_map_dim[name[1]];
                                    }
                                }
                                std::stringstream ss;
                                std::streamsize nsig = int(log10(jmax)) + 1;
                                ss << std::setfill('0') << setw(nsig) << j+1;
                                string name_sed = "Sediment " + ss.str();
                                if (j > 0)
                                {
                                    QString msg = "Multiple fractions not supported, just the first one will be displayed";
                                    QMessageBox::warning(0, QString("Warning"), QString("%1: %2").arg(msg).arg(var_name.c_str()));
                                    //m_nr_mesh_var += 1;
                                    //mesh_vars->variable = (struct _variable **)realloc(mesh_vars->variable, sizeof(struct _variable *) * m_nr_mesh_var);
                                }
                                // reduce dimension (from 3 to 2)
                                mesh_vars->variable[m_nr_mesh_var - 1]->dim_names.resize(2);
                                mesh_vars->variable[m_nr_mesh_var - 1]->dim_names.clear();
                                mesh_vars->variable[m_nr_mesh_var - 1]->dim_names.push_back(name[0]);
                                mesh_vars->variable[m_nr_mesh_var - 1]->dim_names.push_back(name[1]);
                                mesh_vars->variable[m_nr_mesh_var - 1]->dims.resize(2);
                                mesh_vars->variable[m_nr_mesh_var - 1]->dims.clear();
                                mesh_vars->variable[m_nr_mesh_var - 1]->dims.push_back(dims[0]);
                                mesh_vars->variable[m_nr_mesh_var - 1]->dims.push_back(dims[1]);
                            }
                        }
                    }
                }
                else if (mesh_vars->variable[m_nr_mesh_var - 1]->dims.size() == 4)
                {
                    // 4D variable does not contain the time variable, so it is time independent
                    // - time
                    // - sediment dimension
                    // - xy-space dimension
                    // - z-space
                    QString msg = "Multiple fractions are not supported for a 3D simulation.";
                    QMessageBox::warning(0, QString("Warning"), QString("%1").arg(msg));
                }
            }

            /*
            int nr_mesh2d = 1;
            if (var_name == mesh2d_strings[nr_mesh2d - 1]->x_bound_edge_name)
            {
                mesh_vars->variable[m_nr_mesh_var - 1]->location = "edge_boundary";
            }
            if (var_name == mesh2d_strings[nr_mesh2d - 1]->y_bound_edge_name)
            {
                mesh_vars->variable[m_nr_mesh_var - 1]->location = "edge_boundary";
            }
            if (var_name == mesh2d_strings[nr_mesh2d - 1]->x_bound_face_name)
            {
                mesh_vars->variable[m_nr_mesh_var - 1]->location = "face_boundary";
            }
            if (var_name == mesh2d_strings[nr_mesh2d - 1]->y_bound_face_name)
            {
                mesh_vars->variable[m_nr_mesh_var - 1]->location = "face_boundary";
            }
            */
        }
    }

    free(var_name_c);
    var_name_c = nullptr;
#ifndef NATIVE_C
    m_pgBar->setValue(900);
#endif

    return (long) status;
}
//------------------------------------------------------------------------------
struct _mesh_variable * UGRID::get_variables()
{
 #ifdef NATIVE_C
    fprintf(stderr, "UGRID::get_variables()\n");
#endif    
   return this->mesh_vars;
}
//------------------------------------------------------------------------------
struct _mesh_contact * UGRID::get_mesh_contact()
{
#ifdef NATIVE_C
    fprintf(stderr, "UGRID::get_contact_edges()\n");
#endif    
    return this->mesh_contact;
}
//------------------------------------------------------------------------------
struct _ntw_nodes * UGRID::get_connection_nodes()
{
#ifdef NATIVE_C
    fprintf(stderr, "UGRID::get_connection_nodes()\n");
#endif    
    return this->ntw_nodes;
}
//------------------------------------------------------------------------------
struct _ntw_edges * UGRID::get_network_edges()
{
#ifdef NATIVE_C
    fprintf(stderr, "UGRID::get_network_edges()\n");
#endif    
    return this->ntw_edges;
}
//------------------------------------------------------------------------------
struct _ntw_geom * UGRID::get_network_geometry()
{
#ifdef NATIVE_C
    fprintf(stderr, "UGRID::get_network_geometry()\n");
#endif    
    return this->ntw_geom;
}
//------------------------------------------------------------------------------
struct _mesh1d * UGRID::get_mesh1d()
{
#ifdef NATIVE_C
    fprintf(stderr, "UGRID::get_mesh1d()\n");
#endif    
    return mesh1d;
}
//------------------------------------------------------------------------------
struct _mesh2d * UGRID::get_mesh2d()
{
#ifdef NATIVE_C
    fprintf(stderr, "UGRID::get_mesh2d()\n");
#endif    
    return mesh2d;
}
//------------------------------------------------------------------------------
struct _mesh1d_string ** UGRID::get_mesh1d_string()
{
#ifdef NATIVE_C
    fprintf(stderr, "UGRID::get_mesh2d()\n");
#endif    
    return mesh1d_strings;
}
//------------------------------------------------------------------------------
struct _mesh2d_string ** UGRID::get_mesh2d_string()
{
#ifdef NATIVE_C
    fprintf(stderr, "UGRID::get_mesh2d()\n");
#endif    
    return mesh2d_strings;
}
//------------------------------------------------------------------------------
struct _mesh_contact_string ** UGRID::get_mesh_contact_string()
{
#ifdef NATIVE_C
    fprintf(stderr, "UGRID::get_mesh2d()\n");
#endif    
    return mesh_contact_strings;
}
//------------------------------------------------------------------------------
struct _mapping * UGRID::get_grid_mapping()
{
#ifdef NATIVE_C
    fprintf(stderr, "UGRID::get_grid_mapping()\n");
#else
    //QMessageBox::warning(0, QString("Warning"), QString("UGRID::get_grid_mapping()"));
#endif 
    return mapping;
}
//------------------------------------------------------------------------------
struct _variable * UGRID::get_var_by_std_name(struct _mesh_variable * vars, string mesh, string standard_name)
{
    for (int i = 0; i < vars->nr_vars; i++)
    {
        if (standard_name == vars->variable[i]->standard_name && mesh == vars->variable[i]->mesh)
        {
            return vars->variable[i];
        }
    }
    return nullptr;
}

//------------------------------------------------------------------------------
DataValuesProvider2D<double> UGRID::get_variable_values(const string var_name)
// return: 1d dimensional value(x), ie time independent
// return: 2d dimensional value(time, x)
{
    int var_id;
    int status;
    int i_var;
    DataValuesProvider2D<double> data_pointer;

#ifdef NATIVE_C
    fprintf(stderr, "UGRID::get_variable_values()\n");
#endif    

    for (int i = 0; i < mesh_vars->nr_vars; i++)
    {
        if (var_name == mesh_vars->variable[i]->var_name)
        {
            i_var = i;
            if (!mesh_vars->variable[i]->read)  // are the z_values already read
            {
                m_pgBar->show();
                m_pgBar->setValue(0);
                status = nc_inq_varid(this->ncid, var_name.c_str(), &var_id);
                size_t length = 1;
                for (int j = 0; j < mesh_vars->variable[i]->dims.size(); j++)
                {
                    length *= mesh_vars->variable[i]->dims[j];
                }
                double * values_c = (double *)malloc(sizeof(double) * length);
                m_pgBar->setValue(400);
                status = nc_get_var_double(this->ncid, var_id, values_c);
                m_pgBar->setValue(800);
                if (mesh_vars->variable[i]->dims.size() == 1)
                {
                    long time_dim = 1;
                    long xy_dim = mesh_vars->variable[i]->dims[0];
                    DataValuesProvider2D<double> DataValuesProvider2D(values_c, time_dim, xy_dim);
                    mesh_vars->variable[i_var]->data_2d = DataValuesProvider2D;
                }
                else if (mesh_vars->variable[i]->dims.size() == 2)
                {
                    long time_dim = mesh_vars->variable[i]->dims[0];  // TODO: Assumed to be the time dimension
                    long xy_dim = mesh_vars->variable[i]->dims[1];  // TODO: Assumed to be the 2DH space dimension
                    DataValuesProvider2D<double> DataValuesProvider2D(values_c, time_dim, xy_dim);
                    mesh_vars->variable[i_var]->data_2d = DataValuesProvider2D;
                }
                else if (mesh_vars->variable[i]->dims.size() == 3)
                {
                    continue;
                }
                mesh_vars->variable[i]->read = true;
                m_pgBar->setValue(1000);
                m_pgBar->hide();
                return mesh_vars->variable[i_var]->data_2d; // variable value is found
            }
            return mesh_vars->variable[i_var]->data_2d;
        }
    }
    return data_pointer;
}
//------------------------------------------------------------------------------
DataValuesProvider3D<double> UGRID::get_variable_3d_values(const string var_name)
// return: 3d dimensional value(time, layer, x)
{
    int var_id;
    int status;
    int i_var;
    double * read_c;
    double * values_c;
    DataValuesProvider3D<double> data_pointer;
#ifdef NATIVE_C
    fprintf(stderr, "UGRID::get_variable_values()\n");
#endif    

    for (int i = 0; i < mesh_vars->nr_vars; i++)
    {
        if (var_name == mesh_vars->variable[i]->var_name)  //var_name is a three dimensio
        {
            i_var = i; 
            if (!mesh_vars->variable[i]->read)  // are the z_values already read
            {
                m_pgBar->show();
                status = nc_inq_varid(this->ncid, var_name.c_str(), &var_id);
                size_t length = 1;
                for (int j = 0; j < mesh_vars->variable[i]->dims.size(); j++)
                {
                    length *= mesh_vars->variable[i]->dims[j];
                }
                read_c = (double *)malloc(sizeof(double) * length);
                //boost::timer::cpu_timer timer;
                status = nc_get_var_double(this->ncid, var_id, read_c);
                //std::cout << timer.format() << '\n';
                m_pgBar->setValue(100);
                long time_dim = mesh_vars->variable[i]->dims[0];
                long layer_dim = mesh_vars->variable[i]->dims[1];
                long xy_dim = mesh_vars->variable[i]->dims[2];
                bool swap_loops = false;
                //HACK assumed is that the time is the first dimension
                //HACK just the variables at the layers, interfaces are skipped
                if (m_map_dim_name["zs_dim_layer"] == mesh_vars->variable[i]->dim_names[2] ||
                    m_map_dim_name["zs_dim_interface"] == mesh_vars->variable[i]->dim_names[2])
                {
                    // loop over layers and nodes should be swapped
                    swap_loops = true;
                }
                values_c = (double *)malloc(sizeof(double) * length);
                int k_tot = time_dim * xy_dim * layer_dim + xy_dim * layer_dim + layer_dim;
                if (swap_loops)
                {
                    for (int t = 0; t < time_dim; t++)
                    {
                        for (int xy = 0; xy < xy_dim; xy++)
                        {
                            for (int l = 0; l < layer_dim; l++)
                            {
                                int k_target = t * xy_dim * layer_dim  + xy * layer_dim + l;
                                int k_source = t * xy_dim * layer_dim + l * xy_dim + xy;
                                values_c[k_target] = read_c[k_source];

                                double fraction = 100. + 900. * double(k_target) / double(k_tot);
                                m_pgBar->setValue(int(fraction));
                            }
                        }
                    }
                    int tmp_dim = layer_dim;
                    layer_dim = xy_dim;
                    xy_dim = tmp_dim;
                    free(read_c);
                }
                else
                {
                    memcpy(values_c, read_c, length*sizeof(double));
                }
                DataValuesProvider3D<double> DataValuesProvider3D(values_c, time_dim, layer_dim, xy_dim);

                for (int j = 0; j < mesh_vars->variable[i]->dim_names.size(); j++)
                {
                    if (m_map_dim_name["z_sigma_interface"] == mesh_vars->variable[i]->dim_names[j])
                    {
#ifdef NATIVE_C
                        fprintf(stderr, "\t3D data on interfaces not yet supported.\n");
#else
                        QMessageBox::warning(0, QString("Warning"), QString("3D data on interfaces not yet supported,\ndata: %1").arg(var_name.c_str()));
#endif
                        return DataValuesProvider3D;
                    }

                }
                mesh_vars->variable[i_var]->data_3d = DataValuesProvider3D;
                mesh_vars->variable[i]->read = true;
                m_pgBar->hide();
                return mesh_vars->variable[i_var]->data_3d;  // if read, skip all remaining variables
            }
            return mesh_vars->variable[i_var]->data_3d;
        }
    }
    return data_pointer;
}
//==============================================================================
// PRIVATE functions
//==============================================================================
int UGRID::get_attribute_by_var_name(int ncid, string var_name, string att_name, string * att_value)
{
    int status = -1;
    int i_var;
    status = nc_inq_varid(ncid, var_name.c_str(), &i_var);
    status = get_attribute(ncid, i_var, att_name, att_value);
    return status;
}
//------------------------------------------------------------------------------
int UGRID::get_attribute(int ncid, int i_var, char * att_name, char ** att_value)
{
    size_t length = 0;
    int status = -1;

    status = nc_inq_attlen(this->ncid, i_var, att_name, &length);
    *att_value = (char *)malloc(sizeof(char) * (length + 1));
    if (status != NC_NOERR)
    {
        *att_value = '\0';
    }
    else
    {
        status = nc_get_att(this->ncid, i_var, att_name, *att_value);
        att_value[0][length] = '\0';
    }
    return status;
}
//------------------------------------------------------------------------------
int UGRID::get_attribute(int ncid, int i_var, char * att_name, string * att_value)
{
    size_t length = 0;
    int status = -1;

    status = nc_inq_attlen(this->ncid, i_var, att_name, &length);
    if (status != NC_NOERR)
    {
        *att_value = "";
    }
    else
    {
        char * tmp_value = (char *)malloc(sizeof(char) * (length + 1));
        status = nc_get_att(this->ncid, i_var, att_name, tmp_value);
        tmp_value[length] = '\0';
        *att_value = string(tmp_value, length);
        free(tmp_value);
        tmp_value = nullptr;
    }
    return status;
}
//------------------------------------------------------------------------------
int UGRID::get_attribute(int ncid, int i_var, string att_name, string * att_value)
{
    size_t length = 0;
    int status = -1;

    status = nc_inq_attlen(this->ncid, i_var, att_name.c_str(), &length);
    if (status != NC_NOERR)
    {
        *att_value = "";
    }
    else
    {
        char * tmp_value = (char *)malloc(sizeof(char) * (length + 1));
        status = nc_get_att(this->ncid, i_var, att_name.c_str(), tmp_value);
        tmp_value[length] = '\0';
        *att_value = string(tmp_value, length);
        free(tmp_value);
        tmp_value = nullptr;
    }
    return status;
}
//------------------------------------------------------------------------------
int UGRID::get_attribute(int ncid, int i_var, char * att_name, double * att_value)
{
    int status = -1;

    status = nc_get_att_double(this->ncid, i_var, att_name, att_value);

    return status;
}
//------------------------------------------------------------------------------
int UGRID::get_attribute(int ncid, int i_var, char * att_name, int * att_value)
{
    int status = -1;

    status = nc_get_att_int(this->ncid, i_var, att_name, att_value);

    return status;
}
//------------------------------------------------------------------------------
int UGRID::get_attribute(int ncid, int i_var, char * att_name, long * att_value)
{
    int status = -1;

    status = nc_get_att_long(this->ncid, i_var, att_name, att_value);

    return status;
}
//------------------------------------------------------------------------------
int UGRID::get_dimension(int ncid, char * dim_name, size_t * dim_length)
{
    int dimid;
    int status = -1;

    *dim_length = 0;
    if (dim_name != NULL && strlen(dim_name) != 0)
    {
        status = nc_inq_dimid(this->ncid, dim_name, &dimid);
        status = nc_inq_dimlen(this->ncid, dimid, dim_length);
    }
    return status;
}
//------------------------------------------------------------------------------
int UGRID::get_dimension(int ncid, string dim_name, size_t * dim_length)
{
    int dimid;
    int status = -1;

    *dim_length = 0;
    if (dim_name.size() != 0)
    {
        status = nc_inq_dimid(this->ncid, dim_name.c_str(), &dimid);
        status = nc_inq_dimlen(this->ncid, dimid, dim_length);
    }
    return status;
}
//------------------------------------------------------------------------------
int UGRID::get_dimension_var(int ncid, string var_name, size_t * dim_length)
{
    // get the total dimension length in bytes of the var_name variable
    int dimid;
    int status = -1;
    *dim_length = 0;

    if (var_name.size() != 0)
    {
        int janm;
        status = nc_inq_varid(this->ncid, var_name.c_str(), &dimid);
        status = nc_inq_vardimid(this->ncid, dimid, &janm);
        char * tmp_value = (char *)malloc(sizeof(char) * (NC_MAX_NAME + 1));;
        status = nc_inq_dimname(this->ncid, janm, tmp_value);
        status = get_dimension(this->ncid, tmp_value, dim_length);
        free(tmp_value);
        tmp_value = nullptr;
    }
    return status;
}
vector<string>  UGRID::get_dimension_names(int ncid, string var_name)
{
    int dimid;
    int status = -1;
    vector<string> dim_names;

    if (var_name.size() != 0)
    {
        int janm;
        status = nc_inq_varid(this->ncid, var_name.c_str(), &dimid);
        status = nc_inq_vardimid(this->ncid, dimid, &janm);
        char * tmp_value = (char *)malloc(sizeof(char) * (NC_MAX_NAME + 1));;
        status = nc_inq_dimname(this->ncid, janm, tmp_value);
        dim_names.push_back(tmp_value);
        free(tmp_value);
        tmp_value = nullptr;
    }
    return dim_names;
}
//------------------------------------------------------------------------------
vector<string> UGRID::get_names(int ncid, string names, size_t count)
{
    int var_id;
    int status;
    vector<string> token;

    status = nc_inq_varid(ncid, names.c_str(), &var_id);
    if (status == NC_NOERR)
    {
        int ndims[2];
        char * length_name = (char *)malloc(sizeof(char) * (NC_MAX_NAME + 1));;
        status = nc_inq_vardimid(ncid, var_id, (int*)ndims);
        status = nc_inq_dimname(ncid, ndims[1], length_name);
        size_t strlen;
        status = get_dimension(ncid, length_name, &strlen);

        char * c = (char *)malloc(sizeof(char) * (count * strlen));
        status = nc_get_var_text(ncid, var_id, c);

        token = tokenize(c, count);
        free(c);
        c = nullptr;
        free(length_name);
        length_name = nullptr;
    }
    return token;
}
//------------------------------------------------------------------------------
int UGRID::read_variables_with_cf_role(int i_var, string var_name, string cf_role, int ndims, int * var_dimids)
{
    int topology_dimension;
    int status = 1;
    int var_id = -1;

    long nr_ntw = 0;
    long nr_geom = 0;
    long nr_mesh1d = 0;
    long nr_mesh2d = 0;
    long k;

    string att_value;

    if (cf_role == "parent_mesh_topology")  // 1D + 2D mesh
    {
        m_nr_mesh_contacts += 1;
        if (m_nr_mesh_contacts == 1)
        {
            mesh_contact_strings = (struct _mesh_contact_string **)malloc(sizeof(struct _mesh_contact_string *) * m_nr_mesh_contacts);
        }
        else
        {
        }
        mesh_contact_strings[m_nr_mesh_contacts - 1] = new _mesh_contact_string();

        read_composite_mesh_attributes(mesh_contact_strings[m_nr_mesh_contacts - 1], i_var, var_name);

        if (m_nr_mesh_contacts == 1)
        {
            mesh_contact = new _mesh_contact();

            mesh_contact->node = (struct _feature **)malloc(sizeof(struct _feature*));
            mesh_contact->node[m_nr_mesh_contacts - 1] = new _feature();

            mesh_contact->edge = (struct _edge **)malloc(sizeof(struct _edge *));
            mesh_contact->edge[m_nr_mesh_contacts - 1] = new _edge();
        }
        else
        {
        }
        mesh_contact->nr_mesh_contact = m_nr_mesh_contacts;

        status = nc_inq_varid(this->ncid, mesh_contact_strings[m_nr_mesh_contacts - 1]->mesh_contact.c_str(), &var_id);
        int ndims;
        status = nc_inq_varndims(this->ncid, var_id, &ndims);
        int * dimids = (int *)malloc(sizeof(int) * ndims);
        status = nc_inq_vardimid(this->ncid, var_id, dimids);
        if (m_dimids[dimids[0]] != _two)  // one of the dimension is always 2
        {
            mesh_contact->edge[m_nr_mesh_contacts - 1]->count = m_dimids[dimids[0]];
        }
        else
        {
            mesh_contact->edge[m_nr_mesh_contacts - 1]->count = m_dimids[dimids[1]];
        }
        mesh_contact->node[m_nr_mesh_contacts - 1]->count = mesh_contact->edge[m_nr_mesh_contacts - 1]->count * _two;

        //get edge nodes (branch definition between connection nodes)
        contact_edge_nodes = (int *)malloc(sizeof(int) * mesh_contact->edge[m_nr_mesh_contacts - 1]->count * _two);
        mesh_contact->edge[m_nr_mesh_contacts - 1]->edge_nodes = (int **)malloc(sizeof(int *) * mesh_contact->edge[m_nr_mesh_contacts - 1]->count);
        for (int i = 0; i < mesh_contact->edge[m_nr_mesh_contacts - 1]->count; i++)
        {
            mesh_contact->edge[m_nr_mesh_contacts - 1]->edge_nodes[i] = contact_edge_nodes + _two * i;
        }

        size_t start2[] = { 0, 0 };
        size_t count2[] = { mesh_contact->edge[m_nr_mesh_contacts - 1]->count,  _two };
        status = nc_inq_varid(this->ncid, mesh_contact_strings[m_nr_mesh_contacts - 1]->mesh_contact.c_str(), &var_id);
        status = nc_get_vara_int(this->ncid, var_id, start2, count2, contact_edge_nodes);

        int start_index;
        status = get_attribute(this->ncid, var_id, "start_index", &start_index);
        if (status == NC_NOERR)
        {
            if (start_index != 0)
            {
                for (int i = 0; i < mesh_contact->edge[m_nr_mesh_contacts - 1]->count; i++)
                {
                    mesh_contact->edge[m_nr_mesh_contacts - 1]->edge_nodes[i][0] -= start_index;
                    mesh_contact->edge[m_nr_mesh_contacts - 1]->edge_nodes[i][1] -= start_index;
                }
            }
        }

        string att_string;
        status = get_attribute(this->ncid, var_id, "contact", &att_string);
        // search for mesh_a, after semi colon is the location in mesh_a defined
        // search for mesh_b, after semi colon is the location in mesh_b defined
        // example: "mesh1D:node mesh2D:face" or "mesh1D: node mesh2D: face", yes/no a white space behind semi colon

        for (size_t i = 0; i < att_string.size(); ++i) {
            if (att_string[i] == ':') {
                att_string.replace(i, 1, " ");
            }
        }
        vector<string> token = tokenize(att_string, ' ');
        if (token.size() != 4)
        {
#if defined NATIVE_C
#else
            QMessageBox::critical(0, "Error", QString("String \"%1\"should have 4 elements").arg(att_string.c_str()));
#endif
            status = 1;
            return status;
        }
        mesh_contact->mesh_a = strdup(token[0].c_str());
        mesh_contact->location_a = strdup(token[1].c_str());
        mesh_contact->mesh_b = strdup(token[2].c_str());
        mesh_contact->location_b = strdup(token[3].c_str());

        // (x, y) will be filled later, first the 1D nodes along branches should calculated

#ifdef NATIVE_C
        fprintf(stderr, "End of reading contact mesh\n");
#else
        //QMessageBox::information(0, "Information", "End of reading Network");
#endif

    }
    if (cf_role == "mesh_topology")
    {
        // is is a mesh or geometry
        topology_dimension = 0;
        status = get_attribute(this->ncid, i_var, "topology_dimension", &topology_dimension);
        if (topology_dimension == 1)  // it is one dimensional
        {
            string coordinate_space;
            status = get_attribute(this->ncid, i_var, "coordinate_space", &coordinate_space);
            if (status == NC_ENOTATT)  // attribute "coordinate_space" not found, so it is a geometry
            {
                nr_ntw += 1;
                if (nr_ntw == 1)
                {
                    ntw_strings = (struct _ntw_string **)malloc(sizeof(struct _ntw_string *) * nr_ntw);
                }
                else
                {
                }
                ntw_strings[nr_ntw - 1] = new _ntw_string;

                status = read_network_attributes(ntw_strings[nr_ntw - 1], i_var, var_name, topology_dimension);

                if (nr_ntw == 1)
                {
                    ntw_nodes = new struct _ntw_nodes();
                    ntw_nodes->node = (struct _feature **)malloc(sizeof(struct _feature *));
                    ntw_nodes->node[nr_ntw - 1] = new _feature();

                    ntw_edges = new struct _ntw_edges();
                    ntw_edges->edge = (struct _edge **)malloc(sizeof(struct _edge*));
                    ntw_edges->edge[nr_ntw - 1] = new struct _edge();

                    ntw_geom = new _ntw_geom();
                    ntw_geom->geom = (struct _geom **)malloc(sizeof(struct _geom*));
                    ntw_geom->geom[nr_ntw - 1] = new _geom();
                    ntw_geom->geom[nr_ntw - 1]->nodes = (struct _feature **)malloc(sizeof(struct _feature *));
                    ntw_geom->geom[nr_ntw - 1]->nodes[nr_geom - 1] = new _feature();
                }
                else
                {
                }
                ntw_nodes->nr_ntw = nr_ntw;
                ntw_edges->nr_ntw = nr_ntw;

                status = nc_inq_varid(this->ncid, ntw_strings[nr_ntw - 1]->edge_node_connectivity.c_str(), &var_id);
                int ndims;
                status = nc_inq_varndims(this->ncid, var_id, &ndims);
                int * dimids = (int *)malloc(sizeof(int) * ndims);
                status = nc_inq_vardimid(this->ncid, var_id, dimids);
                if (m_dimids[dimids[0]] != _two)  // one of the dimension is always 2
                {
                    ntw_edges->edge[nr_ntw - 1]->count = m_dimids[dimids[0]];
                }
                else
                {
                    ntw_edges->edge[nr_ntw - 1]->count = m_dimids[dimids[1]];
                }
                free(dimids);
                dimids = nullptr;
                // get the branch length
                ntw_edges->edge[nr_ntw - 1]->edge_length = vector<double>(ntw_edges->edge[nr_ntw - 1]->count);

                var_id = -1;
                size_t start1[] = { 0 };
                size_t count1[] = { ntw_edges->edge[nr_ntw - 1]->count };
                status = nc_inq_varid(this->ncid, ntw_strings[nr_ntw - 1]->edge_length.c_str(), &var_id);
                status = nc_get_vara_double(this->ncid, var_id, start1, count1, ntw_edges->edge[nr_ntw - 1]->edge_length.data());

                //get edge nodes (branch definition between connection nodes)
                topo_edge_nodes = (int *)malloc(sizeof(int) * ntw_edges->edge[nr_ntw - 1]->count * _two);
                ntw_edges->edge[nr_ntw - 1]->edge_nodes = (int **)malloc(sizeof(int *) * ntw_edges->edge[nr_ntw - 1]->count);
                for (int i = 0; i < ntw_edges->edge[nr_ntw - 1]->count; i++)
                {
                    ntw_edges->edge[nr_ntw - 1]->edge_nodes[i] = topo_edge_nodes + _two * i;
                }
                size_t start2[] = { 0, 0 };
                size_t count2[] = { ntw_edges->edge[nr_ntw - 1]->count, _two };
                status = nc_inq_varid(this->ncid, ntw_strings[nr_ntw - 1]->edge_node_connectivity.c_str(), &var_id);
                status = nc_get_vara_int(this->ncid, var_id, start2, count2, topo_edge_nodes);

                int start_index;
                status = get_attribute(this->ncid, var_id, "start_index", &start_index);
                if (status == NC_NOERR)
                {
                    if (start_index != 0)
                    {
                        for (int i = 0; i < ntw_edges->edge[nr_ntw - 1]->count; i++)
                        {
                            ntw_edges->edge[nr_ntw - 1]->edge_nodes[i][0] -= start_index;
                            ntw_edges->edge[nr_ntw - 1]->edge_nodes[i][1] -= start_index;
                        }
                    }
                }

                /* Read the data (x, y)-coordinate of each node */
                status = get_dimension_var(this->ncid, ntw_strings[nr_ntw - 1]->x_ntw_name, &ntw_nodes->node[nr_ntw - 1]->count);

                ntw_nodes->node[nr_ntw - 1]->x = vector<double>(ntw_nodes->node[nr_ntw - 1]->count);
                ntw_nodes->node[nr_ntw - 1]->y = vector<double>(ntw_nodes->node[nr_ntw - 1]->count);

                status = nc_inq_varid(this->ncid, ntw_strings[nr_ntw - 1]->x_ntw_name.c_str(), &var_id);
                status = get_attribute(this->ncid, var_id, "standard_name", &att_value);
                if (att_value == "projection_x_coordinate" || att_value == "longitude")
                {
                    status = nc_get_var_double(this->ncid, var_id, ntw_nodes->node[nr_ntw - 1]->x.data());
                    status = nc_inq_varid(this->ncid, ntw_strings[nr_ntw - 1]->y_ntw_name.c_str(), &var_id);
                    status = nc_get_var_double(this->ncid, var_id, ntw_nodes->node[nr_ntw - 1]->y.data());
                }
                else
                {
                    status = nc_get_var_double(this->ncid, var_id, ntw_nodes->node[nr_ntw - 1]->y.data());
                    status = nc_inq_varid(this->ncid, ntw_strings[nr_ntw - 1]->y_ntw_name.c_str(), &var_id);
                    status = nc_get_var_double(this->ncid, var_id, ntw_nodes->node[nr_ntw - 1]->x.data());
                }

                /////////////////////////////////////////////////////////////////////

                ntw_nodes->node[nr_ntw - 1]->name = get_names(this->ncid, ntw_strings[nr_ntw - 1]->node_names, ntw_nodes->node[nr_ntw - 1]->count);
                ntw_nodes->node[nr_ntw - 1]->long_name = get_names(this->ncid, ntw_strings[nr_ntw - 1]->node_long_names, ntw_nodes->node[nr_ntw - 1]->count);

                /////////////////////////////////////////////////////////////////////

                ntw_edges->edge[nr_ntw - 1]->name = get_names(this->ncid, ntw_strings[nr_ntw - 1]->edge_names, ntw_edges->edge[nr_ntw - 1]->count);
                ntw_edges->edge[nr_ntw - 1]->long_name = get_names(this->ncid, ntw_strings[nr_ntw - 1]->edge_long_names, ntw_edges->edge[nr_ntw - 1]->count);

                /////////////////////////////////////////////////////////////////////

                // get the geometry of the network
                nr_geom += 1;
                if (nr_geom == 1)
                {
                    geom_strings = (struct _geom_string **)malloc(sizeof(struct _geom_string *) * nr_ntw);
                }
                else
                {
                }
                geom_strings[nr_geom - 1] = new _geom_string;

                status = nc_inq_varid(this->ncid, ntw_strings[nr_ntw - 1]->edge_geometry.c_str(), &var_id);
                status = read_geometry_attributes(geom_strings[nr_ntw-1], var_id, ntw_strings[nr_ntw - 1]->edge_geometry, topology_dimension);
                if (status != 0) {
                    return status;
                }

                status = nc_inq_varid(this->ncid, geom_strings[nr_ntw - 1]->node_count.c_str(), &var_id);  // nodes per edge
                status = nc_inq_varndims(this->ncid, var_id, &ndims);
                dimids = (int *)malloc(sizeof(int) * ndims);
                status = nc_inq_vardimid(this->ncid, var_id, dimids);
                ntw_geom->geom[nr_ntw - 1]->count = m_dimids[dimids[0]];
                free(dimids);
                dimids = nullptr;

                /* Read the data (x, y)-coordinates of the geometries */
                status = nc_inq_varid(this->ncid, geom_strings[nr_ntw - 1]->node_count.c_str(), &var_id);
                int * geom_node_count = (int *)malloc(sizeof(int) * ntw_geom->geom[nr_ntw - 1]->count);
                status = nc_get_var_int(this->ncid, var_id, geom_node_count);

                ntw_geom->geom[nr_ntw - 1]->nodes = (_feature **)malloc(sizeof(_feature *) * ntw_geom->geom[nr_ntw - 1]->count);
                for (int i = 0; i < ntw_geom->geom[nr_ntw - 1]->count; i++)
                {
                    ntw_geom->geom[nr_ntw - 1]->nodes[i] = new _feature;
                    ntw_geom->geom[nr_ntw - 1]->nodes[i]->count = geom_node_count[i];
                    ntw_geom->geom[nr_ntw - 1]->nodes[i]->x = vector<double>(geom_node_count[i]);
                    ntw_geom->geom[nr_ntw - 1]->nodes[i]->y = vector<double>(geom_node_count[i]);
                }
                ntw_geom->nr_ntw = nr_ntw;

                // read complete x, y array
                status = nc_inq_varid(this->ncid, geom_strings[nr_ntw - 1]->x_geom_name.c_str(), &var_id);
                status = nc_inq_varndims(this->ncid, var_id, &ndims);
                dimids = (int *)malloc(sizeof(int) * ndims);
                status = nc_inq_vardimid(this->ncid, var_id, dimids);

                vector<double> x = vector<double>(m_dimids[dimids[0]]);
                vector<double> y = vector<double>(m_dimids[dimids[0]]);

                status = nc_inq_varid(this->ncid, geom_strings[nr_ntw - 1]->x_geom_name.c_str(), &var_id);
                status = get_attribute(this->ncid, var_id, "standard_name", &att_value);
                if (att_value == "projection_x_coordinate" || att_value == "longitude")
                {
                    status = nc_get_var_double(this->ncid, var_id, x.data());
                    status = nc_inq_varid(this->ncid, geom_strings[nr_ntw - 1]->y_geom_name.c_str(), &var_id);
                    status = nc_get_var_double(this->ncid, var_id, y.data());
                }
                else
                {
                    status = nc_get_var_double(this->ncid, var_id, y.data());
                    status = nc_inq_varid(this->ncid, geom_strings[nr_ntw - 1]->y_geom_name.c_str(), &var_id);
                    status = nc_get_var_double(this->ncid, var_id, x.data());
                }


                // dispatch the x, y array over the branches
                k = -1;
                for (int i = 0; i < ntw_geom->geom[nr_ntw - 1]->count; i++)
                {
                    for (int j = 0; j < ntw_geom->geom[nr_ntw - 1]->nodes[i]->count; j++)
                    {
                        k += 1;
                        ntw_geom->geom[nr_ntw - 1]->nodes[i]->x[j] = x[k];
                        ntw_geom->geom[nr_ntw - 1]->nodes[i]->y[j] = y[k];
                    }
                }

                /////////////////////////////////////////////////////////////////////

                ntw_geom->geom[nr_ntw - 1]->name = get_names(this->ncid, ntw_strings[nr_ntw - 1]->edge_names, ntw_geom->geom[nr_ntw - 1]->count);
                ntw_geom->geom[nr_ntw - 1]->long_name = get_names(this->ncid, ntw_strings[nr_ntw - 1]->edge_long_names, ntw_geom->geom[nr_ntw - 1]->count);

                /////////////////////////////////////////////////////////////////////

#ifdef NATIVE_C
                fprintf(stderr, "End of reading network\n");
#else
                //QMessageBox::information(0, "Information", "End of reading Network");
#endif
            }
            else
                ///////////////////////////////////////////////////////////////////////////////////////////
            {
                // it is a 1 dimensional mesh because it contains a coordinate space
#ifdef NATIVE_C
                fprintf(stderr, "Start of reading 1D Mesh\n");
#else
                //QMessageBox::information(0, "Information", "Start of reading 1D Mesh");
#endif
                nr_mesh1d += 1;
                if (nr_mesh1d == 1)
                {
                    mesh1d_strings = (struct _mesh1d_string **)malloc(sizeof(struct _mesh1d_string *) * nr_mesh1d);
                }
                else
                {
                }
                mesh1d_strings[nr_mesh1d - 1] = new _mesh1d_string();

                status = read_mesh1d_attributes(mesh1d_strings[nr_mesh1d - 1], i_var, var_name, topology_dimension);

                if (nr_mesh1d == 1)
                {
                    mesh1d = new _mesh1d();
                    mesh1d->node = (struct _feature **)malloc(sizeof(struct _feature *));
                    mesh1d->node[nr_mesh1d - 1] = new _feature();

                    mesh1d->edge = (struct _edge **)malloc(sizeof(struct _edge *));
                    mesh1d->edge[nr_mesh1d - 1] = new _edge();
                }
                else
                {
                }
                mesh1d->nr_mesh1d = nr_mesh1d;

#ifdef NATIVE_C
                fprintf(stderr, "\tVariables with \'mesh_topology\' attribute: %s\n", var_name.c_str());
#endif

                //get edge nodes
                status = nc_inq_varid(this->ncid, mesh1d_strings[nr_mesh1d - 1]->edge_node_connectivity.c_str(), &var_id);
                int start_index;

                if (status == NC_NOERR)
                {
                    int ndims;
                    status = nc_inq_varndims(this->ncid, var_id, &ndims);
                    int * dimids = (int *)malloc(sizeof(int) * ndims);
                    status = nc_inq_vardimid(this->ncid, var_id, dimids);
                    if (m_dimids[dimids[0]] != _two)  // one of the dimension is always 2
                    {
                        mesh1d->edge[nr_mesh1d - 1]->count = m_dimids[dimids[0]];
                    }
                    else
                    {
                        mesh1d->edge[nr_mesh1d - 1]->count = m_dimids[dimids[1]];
                    }
                    free(dimids);
                    dimids = nullptr;

                    mesh1d_edge_nodes = (int *)malloc(sizeof(int) * mesh1d->edge[nr_mesh1d - 1]->count * _two);
                    mesh1d->edge[nr_mesh1d - 1]->edge_nodes = (int **)malloc(sizeof(int *) * mesh1d->edge[nr_mesh1d - 1]->count);
                    for (int i = 0; i < mesh1d->edge[nr_mesh1d - 1]->count; i++)
                    {
                        mesh1d->edge[nr_mesh1d - 1]->edge_nodes[i] = mesh1d_edge_nodes + _two * i;
                    }
                    size_t start2[] = { 0, 0 };
                    size_t count2[] = { mesh1d->edge[nr_mesh1d - 1]->count, _two };
                    status = nc_inq_varid(this->ncid, mesh1d_strings[nr_mesh1d - 1]->edge_node_connectivity.c_str(), &var_id);
                    status = nc_get_vara_int(this->ncid, var_id, start2, count2, mesh1d_edge_nodes);

                    status = get_attribute(this->ncid, var_id, "start_index", &start_index);
                    if (status == NC_NOERR)
                    {
                        if (start_index != 0)
                        {
                            for (int i = 0; i < mesh1d->edge[nr_mesh1d - 1]->count; i++)
                            {
                                mesh1d->edge[nr_mesh1d - 1]->edge_nodes[i][0] -= start_index;
                                mesh1d->edge[nr_mesh1d - 1]->edge_nodes[i][1] -= start_index;
                            }
                        }
                    }
                }
                else
                {
                    QMessageBox::information(0, "Information", "edge nodes connectivity not on file");
                    mesh1d->edge[nr_mesh1d - 1]->count = 10;
                    mesh1d_edge_nodes = (int *)malloc(sizeof(int) * mesh1d->edge[nr_mesh1d - 1]->count * _two);
                    mesh1d->edge[nr_mesh1d - 1]->edge_nodes = (int **)malloc(sizeof(int *) * mesh1d->edge[nr_mesh1d - 1]->count);
                    for (int i = 0; i < mesh1d->edge[nr_mesh1d - 1]->count; i++)
                    {
                        mesh1d->edge[nr_mesh1d - 1]->edge_nodes[i] = mesh1d_edge_nodes + _two * i;
                    }
                    for (int i = 0; i < mesh1d->edge[nr_mesh1d - 1]->count; i++)
                    {
                        mesh1d->edge[nr_mesh1d - 1]->edge_nodes[i][0] = 0;
                        mesh1d->edge[nr_mesh1d - 1]->edge_nodes[i][1] = 1;
                    }
                }
                /* Read the data branch id and chainage of each node */
                /* Read the data (x, y)-coordinate of each node */
                if (mesh1d_strings[nr_mesh1d - 1]->node_branch != "")
                {
                    status = get_dimension_var(this->ncid, mesh1d_strings[nr_mesh1d - 1]->node_branch, &mesh1d->node[nr_mesh1d - 1]->count);

                    status = nc_inq_varid(this->ncid, mesh1d_strings[nr_mesh1d - 1]->node_branch.c_str(), &var_id);
                    mesh1d->node[nr_mesh1d - 1]->branch = vector<long>(mesh1d->node[nr_mesh1d - 1]->count);
                    status = nc_get_var_long(this->ncid, var_id, mesh1d->node[nr_mesh1d - 1]->branch.data());

                    status = nc_get_att_int(this->ncid, var_id, "start_index", &start_index);
                    if (start_index != 0)
                    {
                        for (int i = 0; i < mesh1d->node[nr_mesh1d - 1]->count; i++)
                        {
                            mesh1d->node[nr_mesh1d - 1]->branch[i] -= start_index;
                        }
                    }

                    mesh1d->node[nr_mesh1d - 1]->chainage = vector<double>(mesh1d->node[nr_mesh1d - 1]->count);
                    status = nc_inq_varid(this->ncid, mesh1d_strings[nr_mesh1d - 1]->node_chainage.c_str(), &var_id);
                    status = nc_get_var_double(this->ncid, var_id, mesh1d->node[nr_mesh1d - 1]->chainage.data());
                }
                else
                {
                    status = get_dimension_var(this->ncid, mesh1d_strings[nr_mesh1d - 1]->x_node_name, &mesh1d->node[nr_mesh1d - 1]->count);
                    mesh1d->node[nr_mesh1d - 1]->x = vector<double>(mesh1d->node[nr_mesh1d - 1]->count);
                    mesh1d->node[nr_mesh1d - 1]->y = vector<double>(mesh1d->node[nr_mesh1d - 1]->count);
                    status = nc_inq_varid(this->ncid, mesh1d_strings[nr_mesh1d - 1]->x_node_name.c_str(), &var_id);
                    status = nc_get_var_double(this->ncid, var_id, mesh1d->node[nr_mesh1d - 1]->x.data());
                    status = nc_inq_varid(this->ncid, mesh1d_strings[nr_mesh1d - 1]->y_node_name.c_str(), &var_id);
                    status = nc_get_var_double(this->ncid, var_id, mesh1d->node[nr_mesh1d - 1]->y.data());
                }
                /* Read the data of each edge */
                if (mesh1d_strings[nr_mesh1d - 1]->edge_branch != "")
                {
                    // read edge_branch array from file
                    status = get_dimension_var(this->ncid, mesh1d_strings[nr_mesh1d - 1]->edge_branch, &mesh1d->edge[nr_mesh1d - 1]->count);

                    status = nc_inq_varid(this->ncid, mesh1d_strings[nr_mesh1d - 1]->edge_branch.c_str(), &var_id);
                    mesh1d->edge[nr_mesh1d - 1]->edge_branch = vector<long>(mesh1d->edge[nr_mesh1d - 1]->count, -1);
                    status = nc_get_var_long(this->ncid, var_id, mesh1d->edge[nr_mesh1d - 1]->edge_branch.data());

                    status = nc_get_att_int(this->ncid, var_id, "start_index", &start_index);
                    if (start_index != 0)
                    {
                        for (int i = 0; i < mesh1d->edge[nr_mesh1d - 1]->count; i++)
                        {
                            mesh1d->edge[nr_mesh1d - 1]->edge_branch[i] -= start_index;
                        }
                    }
                    // determine the edge length between the nodes (by definition >= 0)
                    mesh1d->edge[nr_mesh1d - 1]->edge_length = vector<double>(mesh1d->edge[nr_mesh1d - 1]->count, -1.0);
                    for (int j = 0; j < mesh1d->edge[nr_mesh1d - 1]->count; j++)
                    {
                        int j_branch = mesh1d->edge[nr_mesh1d - 1]->edge_branch[j];
                        int p1 = mesh1d->edge[0]->edge_nodes[j][0];
                        int p2 = mesh1d->edge[0]->edge_nodes[j][1];
                        if (mesh1d->node[0]->branch[p1] == mesh1d->node[0]->branch[p2])
                        {
                            mesh1d->edge[nr_mesh1d - 1]->edge_length[j] = mesh1d->node[0]->chainage[p2] - mesh1d->node[0]->chainage[p1];
                        }
                        else if (mesh1d->node[0]->branch[p1] == j_branch)
                        {
                            // p1 on branch, p2 not on branch, so it is the last edge on a branch
                            mesh1d->edge[nr_mesh1d - 1]->edge_length[j] = ntw_edges->edge[0]->edge_length[j_branch] - mesh1d->node[0]->chainage[p1];
                        }
                        else if (mesh1d->node[0]->branch[p2] == j_branch)
                        {
                            // p1 not on branch, p2 on branch, so it is the first edge on a branch
                            mesh1d->edge[nr_mesh1d - 1]->edge_length[j] = mesh1d->node[0]->chainage[p2];
                        }
                        else
                        {
                            // branch length is equal long to the geometry branch length
                            mesh1d->edge[nr_mesh1d - 1]->edge_length[j] = ntw_edges->edge[0]->edge_length[j_branch];
                        }
                    }
                }
                else
                {
                    // determine the edge_branch array
                    // not done yet, edge_length set to -1
                    mesh1d->edge[nr_mesh1d - 1]->edge_branch = vector<long>(mesh1d->edge[nr_mesh1d - 1]->count, -1);
                    mesh1d->edge[nr_mesh1d - 1]->edge_length = vector<double>(mesh1d->edge[nr_mesh1d - 1]->count, -1.0);
                }

                /////////////////////////////////////////////////////////////////////

                mesh1d->node[nr_mesh1d - 1]->name = get_names(this->ncid, mesh1d_strings[nr_mesh1d - 1]->node_names, mesh1d->node[nr_mesh1d - 1]->count);
                mesh1d->node[nr_mesh1d - 1]->long_name = get_names(this->ncid, mesh1d_strings[nr_mesh1d - 1]->node_long_names, mesh1d->node[nr_mesh1d - 1]->count);

                /////////////////////////////////////////////////////////////////////

                mesh1d->edge[nr_mesh1d - 1]->name = get_names(this->ncid, mesh1d_strings[nr_mesh1d - 1]->edge_names, mesh1d->edge[nr_mesh1d - 1]->count);
                mesh1d->edge[nr_mesh1d - 1]->long_name = get_names(this->ncid, mesh1d_strings[nr_mesh1d - 1]->edge_long_names, mesh1d->edge[nr_mesh1d - 1]->count);

                /////////////////////////////////////////////////////////////////////
#ifdef NATIVE_C
                fprintf(stderr, "End of reading 1D mesh\n");
#else
                //QMessageBox::information(0, "Information", "End of reading 1D mesh");
#endif
            }
        }
        ///////////////////////////////////////////////////////////////////////////////////////////
        if (topology_dimension == 2)  // it is a unstructured mesh
        {
            nr_mesh2d += 1;
            if (nr_mesh2d == 1)
            {
                mesh2d_strings = (struct _mesh2d_string **)malloc(sizeof(struct _mesh2d_string *) * nr_mesh2d);
            }
            else
            {
            }
            mesh2d_strings[nr_mesh2d - 1] = new _mesh2d_string;

            status = read_mesh2d_attributes(mesh2d_strings[nr_mesh2d - 1], i_var, var_name, topology_dimension);

            if (nr_mesh2d == 1)
            {
                mesh2d = new _mesh2d();
                mesh2d->node = (struct _feature **)malloc(sizeof(struct _feature *));
                mesh2d->node[nr_mesh2d - 1] = new _feature();

                mesh2d->edge = (struct _edge **)malloc(sizeof(struct _edge *));
                mesh2d->edge[nr_mesh2d - 1] = new _edge();

                mesh2d->face = (struct _feature **)malloc(sizeof(struct _feature *));
                mesh2d->face[nr_mesh2d - 1] = new _feature();
            }
            else
            {
            }
            mesh2d->nr_mesh2d = nr_mesh2d;

#ifdef NATIVE_C
            fprintf(stderr, "\tVariables with \'mesh_topology\' attribute: %s\n", var_name.c_str());
#endif

            //get edge nodes, optional required
            int ndims;
            int * dimids;
            int start_index;
            if (mesh2d_strings[nr_mesh2d - 1]->edge_node_connectivity != "")
            {
                status = nc_inq_varid(this->ncid, mesh2d_strings[nr_mesh2d - 1]->edge_node_connectivity.c_str(), &var_id);
                status = nc_inq_varndims(this->ncid, var_id, &ndims);
                dimids = (int *)malloc(sizeof(int) * ndims);
                status = nc_inq_vardimid(this->ncid, var_id, dimids);
                if (m_dimids[dimids[0]] != _two)  // one of the dimension is always 2
                {
                    mesh2d->edge[nr_mesh2d - 1]->count = m_dimids[dimids[0]];
                }
                else
                {
                    mesh2d->edge[nr_mesh2d - 1]->count = m_dimids[dimids[1]];
                }

                mesh2d_edge_nodes = (int *)malloc(sizeof(int) * mesh2d->edge[nr_mesh2d - 1]->count * _two);
                mesh2d->edge[nr_mesh2d - 1]->edge_nodes = (int **)malloc(sizeof(int *) * mesh2d->edge[nr_mesh2d - 1]->count);
                for (int i = 0; i < mesh2d->edge[nr_mesh2d - 1]->count; i++)
                {
                    mesh2d->edge[nr_mesh2d - 1]->edge_nodes[i] = mesh2d_edge_nodes + _two * i;
                }
                size_t start2[] = { 0, 0 };
                size_t count2[] = { mesh2d->edge[nr_mesh2d - 1]->count, _two };
                status = nc_inq_varid(this->ncid, mesh2d_strings[nr_mesh2d - 1]->edge_node_connectivity.c_str(), &var_id);
                status = nc_get_vara_int(this->ncid, var_id, start2, count2, mesh2d_edge_nodes);

                // get the branch length
                mesh2d->edge[nr_mesh2d - 1]->edge_length = vector<double>(mesh2d->edge[nr_mesh2d - 1]->count);

                status = get_attribute(this->ncid, var_id, "start_index", &start_index);
                if (status == NC_NOERR)
                {
                    if (start_index != 0)
                    {
                        for (int i = 0; i < mesh2d->edge[nr_mesh2d - 1]->count; i++)
                        {
                            mesh2d->edge[nr_mesh2d - 1]->edge_nodes[i][0] -= start_index;
                            mesh2d->edge[nr_mesh2d - 1]->edge_nodes[i][1] -= start_index;
                        }
                    }
                }
                free(dimids);
                dimids = nullptr;
            }

            /* Read the data (x, y)-coordinate of each node */
            status = get_dimension_var(this->ncid, mesh2d_strings[nr_mesh2d - 1]->x_node_name, &mesh2d->node[nr_mesh2d - 1]->count);

            mesh2d->node[nr_mesh2d - 1]->x = vector<double>(mesh2d->node[nr_mesh2d - 1]->count);
            mesh2d->node[nr_mesh2d - 1]->y = vector<double>(mesh2d->node[nr_mesh2d - 1]->count);
            status = nc_inq_varid(this->ncid, mesh2d_strings[nr_mesh2d - 1]->x_node_name.c_str(), &var_id);
            status = get_attribute(this->ncid, var_id, "standard_name", &att_value);
            if (att_value == "projection_x_coordinate" || att_value == "longitude")
            {
                status = nc_get_var_double(this->ncid, var_id, mesh2d->node[nr_mesh2d - 1]->x.data());
                status = nc_inq_varid(this->ncid, mesh2d_strings[nr_mesh2d - 1]->y_node_name.c_str(), &var_id);
                status = nc_get_var_double(this->ncid, var_id, mesh2d->node[nr_mesh2d - 1]->y.data());
            }
            else
            {
                status = nc_get_var_double(this->ncid, var_id, mesh2d->node[nr_mesh2d - 1]->y.data());
                status = nc_inq_varid(this->ncid, mesh2d_strings[nr_mesh2d - 1]->y_node_name.c_str(), &var_id);
                status = nc_get_var_double(this->ncid, var_id, mesh2d->node[nr_mesh2d - 1]->x.data());
            }

            /* Read the data (x, y)-coordinate of each edge */
            if (mesh2d_strings[nr_mesh2d - 1]->x_edge_name != "")
            {
                status = get_dimension_var(this->ncid, mesh2d_strings[nr_mesh2d - 1]->x_edge_name, &mesh2d->edge[nr_mesh2d - 1]->count);
                if (mesh2d->edge[nr_mesh2d - 1]->count != 0)  // not required attribute
                {
                    mesh2d->edge[nr_mesh2d - 1]->x = vector<double>(mesh2d->edge[nr_mesh2d - 1]->count);
                    mesh2d->edge[nr_mesh2d - 1]->y = vector<double>(mesh2d->edge[nr_mesh2d - 1]->count);
                    status = nc_inq_varid(this->ncid, mesh2d_strings[nr_mesh2d - 1]->x_edge_name.c_str(), &var_id);
                    status = get_attribute(this->ncid, var_id, "standard_name", &att_value);
                    if (att_value == "projection_x_coordinate" || att_value == "longitude")
                    {
                        status = nc_get_var_double(this->ncid, var_id, mesh2d->edge[nr_mesh2d - 1]->x.data());
                        status = nc_inq_varid(this->ncid, mesh2d_strings[nr_mesh2d - 1]->y_edge_name.c_str(), &var_id);
                        status = nc_get_var_double(this->ncid, var_id, mesh2d->edge[nr_mesh2d - 1]->y.data());
                    }
                    else
                    {
                        status = nc_get_var_double(this->ncid, var_id, mesh2d->edge[nr_mesh2d - 1]->y.data());
                        status = nc_inq_varid(this->ncid, mesh2d_strings[nr_mesh2d - 1]->y_edge_name.c_str(), &var_id);
                        status = nc_get_var_double(this->ncid, var_id, mesh2d->edge[nr_mesh2d - 1]->x.data());
                    }

                    status = get_attribute_by_var_name(this->ncid, mesh2d_strings[nr_mesh2d - 1]->x_edge_name, "bounds", &mesh2d_strings[nr_mesh2d - 1]->x_bound_edge_name);
                    status = get_attribute_by_var_name(this->ncid, mesh2d_strings[nr_mesh2d - 1]->y_edge_name, "bounds", &mesh2d_strings[nr_mesh2d - 1]->y_bound_edge_name);
                    int a = 1;
                }
            }
            else
            {
                // Compute the edge coordinates from the node coordinates, halfway on distance between nodes. 
                // Boundary of the edge is determined by the node coordinates (edge_nodes)
                status = get_dimension_var(this->ncid, mesh2d_strings[nr_mesh2d - 1]->x_edge_name, &mesh2d->edge[nr_mesh2d - 1]->count);
                if (mesh2d->edge[nr_mesh2d - 1]->count != 0)  // not required attribute
                {
                    int p1, p2;
                    double x1, x2;
                    double y1, y2;
                    for (int j = 0; j < mesh2d->edge[0]->count; j++)
                    {
                        p1 = mesh2d->edge[0]->edge_nodes[j][0];
                        p2 = mesh2d->edge[0]->edge_nodes[j][1];
                        x1 = mesh2d->node[0]->x[p1];
                        y1 = mesh2d->node[0]->y[p1];
                        x2 = mesh2d->node[0]->x[p2];
                        y2 = mesh2d->node[0]->y[p2];

                        mesh2d->edge[nr_mesh2d - 1]->x.push_back(0.5*(x1 + x2));
                        mesh2d->edge[nr_mesh2d - 1]->y.push_back(0.5*(y1 + y2));
                    }
                }
            }

            /* Read the data (x, y)-coordinate of each face */
            if (mesh2d_strings[nr_mesh2d - 1]->x_face_name != "")
            {

                status = get_dimension_var(this->ncid, mesh2d_strings[nr_mesh2d - 1]->x_face_name, &mesh2d->face[nr_mesh2d - 1]->count);

                if (mesh2d->face[nr_mesh2d - 1]->count != 0)  // not required attribute
                {
                    mesh2d->face[nr_mesh2d - 1]->x = vector<double>(mesh2d->face[nr_mesh2d - 1]->count);
                    mesh2d->face[nr_mesh2d - 1]->y = vector<double>(mesh2d->face[nr_mesh2d - 1]->count);
                    status = nc_inq_varid(this->ncid, mesh2d_strings[nr_mesh2d - 1]->x_face_name.c_str(), &var_id);
                    status = get_attribute(this->ncid, var_id, "standard_name", &att_value);
                    if (att_value == "projection_x_coordinate" || att_value == "longitude")
                    {
                        status = nc_get_var_double(this->ncid, var_id, mesh2d->face[nr_mesh2d - 1]->x.data());
                        status = nc_inq_varid(this->ncid, mesh2d_strings[nr_mesh2d - 1]->y_face_name.c_str(), &var_id);
                        status = nc_get_var_double(this->ncid, var_id, mesh2d->face[nr_mesh2d - 1]->y.data());
                    }
                    else
                    {
                        status = nc_get_var_double(this->ncid, var_id, mesh2d->face[nr_mesh2d - 1]->y.data());
                        status = nc_inq_varid(this->ncid, mesh2d_strings[nr_mesh2d - 1]->y_face_name.c_str(), &var_id);
                        status = nc_get_var_double(this->ncid, var_id, mesh2d->face[nr_mesh2d - 1]->x.data());
                    }
                    string janm;
                    status = get_attribute_by_var_name(this->ncid, mesh2d_strings[nr_mesh2d - 1]->x_face_name, "bounds", &mesh2d_strings[nr_mesh2d - 1]->x_bound_face_name);
                    status = get_attribute_by_var_name(this->ncid, mesh2d_strings[nr_mesh2d - 1]->y_face_name, "bounds", &mesh2d_strings[nr_mesh2d - 1]->y_bound_face_name);
                    int a = 1;
                }
            }
            else
            {
                // Compute the face coordinates from the node coordinates, mass centre of face. But is it needed?
                // Boundary of the face is determined by the node coordinates (face_nodes)
            }

            /* Read the nodes indices for each face */
            status = nc_inq_varid(this->ncid, mesh2d_strings[nr_mesh2d - 1]->face_node_connectivity.c_str(), &var_id);
            status = nc_inq_varndims(this->ncid, var_id, &ndims);
            dimids = (int *)malloc(sizeof(int) * ndims);
            status = nc_inq_vardimid(this->ncid, var_id, dimids);
            size_t length = 1;
            for (int i = 0; i < ndims; i++)
            {
                length *= m_dimids[dimids[i]];
            }
            int * values_c = (int *)malloc(sizeof(int) * length);
            status = nc_get_var_int(this->ncid, var_id, values_c);

            mesh2d->face_nodes.reserve(length);
            status = get_attribute(this->ncid, var_id, "start_index", &start_index);
            if (status != NC_NOERR)
            {
                start_index = 0;
            }
            vector<int> value;
            int k = -1;
            for (int m = 0; m < m_dimids[dimids[0]]; m++)  // faces
            {
                for (int n = 0; n < m_dimids[dimids[1]]; n++)  // nodes
                {
                    k++;
                    int i_node = *(values_c + k) - start_index;
                    value.push_back(i_node);
                }
                mesh2d->face_nodes.push_back(value);
                value.clear();
            }
            // start_index

            // if edge connectivity is not given create the arrays for the face_node_connectivity array which is required by the UGRID standard
            if (mesh2d_strings[nr_mesh2d - 1]->edge_node_connectivity == "")
            {
                // TODO improve the performance of this algorithm
                int max_edges_per_face = m_dimids[dimids[0]] + m_dimids[dimids[1]] - mesh2d->face_nodes.size();  // only applicable if number of dimensions is two
                int total_edges = m_dimids[dimids[0]] * m_dimids[dimids[1]];
                mesh2d_edge_nodes = (int *)malloc(sizeof(int) * total_edges * _two);
                mesh2d->edge[nr_mesh2d - 1]->edge_nodes = (int **)malloc(sizeof(int *) *total_edges);
                for (int i = 0; i < total_edges; i++)
                {
                    mesh2d->edge[nr_mesh2d - 1]->edge_nodes[i] = mesh2d_edge_nodes + _two * i;
                }
                
                int edge_i;
                int jp;
                for (int k = 0; k < mesh2d->face_nodes.size(); k++)
                {
                    for (int j = 0; j < max_edges_per_face; j++)
                    {
                        edge_i = k * max_edges_per_face + j;  // edge number
                        jp = j < max_edges_per_face-1 ? j + 1 : 0;
                        mesh2d->edge[nr_mesh2d - 1]->edge_nodes[edge_i][0] = mesh2d->face_nodes[k][j];
                        mesh2d->edge[nr_mesh2d - 1]->edge_nodes[edge_i][1] = mesh2d->face_nodes[k][jp];
                    }
                }
                mesh2d->edge[nr_mesh2d - 1]->count = mesh2d->face_nodes.size() * max_edges_per_face;

                // Edge array contains double entries: ex. a->b and b->a then retain a->b
                int cnt = 0;
                for (int i = 0; i < mesh2d->edge[nr_mesh2d - 1]->count; i++)
                {
                    for (int j = i + 1; j < mesh2d->edge[nr_mesh2d - 1]->count; j++)
                    {
                        if (mesh2d->edge[nr_mesh2d - 1]->edge_nodes[i][0] == mesh2d->edge[nr_mesh2d - 1]->edge_nodes[j][1] &&
                            mesh2d->edge[nr_mesh2d - 1]->edge_nodes[i][1] == mesh2d->edge[nr_mesh2d - 1]->edge_nodes[j][0])
                        {
                            mesh2d->edge[nr_mesh2d - 1]->edge_nodes[j][0] = -1;
                            mesh2d->edge[nr_mesh2d - 1]->edge_nodes[j][1] = -1;
                            cnt += 1;
                            break;
                        }
                    }
                }
                int k = 0;
                for (int i = 0; i < mesh2d->edge[nr_mesh2d - 1]->count; i++)
                {
                    if (mesh2d->edge[nr_mesh2d - 1]->edge_nodes[i][0] != -1 &&
                        mesh2d->edge[nr_mesh2d - 1]->edge_nodes[i][1] != -1)
                    {
                        mesh2d->edge[nr_mesh2d - 1]->edge_nodes[k][0] = mesh2d->edge[nr_mesh2d - 1]->edge_nodes[i][0];
                        mesh2d->edge[nr_mesh2d - 1]->edge_nodes[k][1] = mesh2d->edge[nr_mesh2d - 1]->edge_nodes[i][1];
                        k++;
                    }
                }
                mesh2d->edge[nr_mesh2d - 1]->count = k;
            }
            free(dimids);
            dimids = nullptr;

            // Do not name the mesh2d nodes, because the 2D mesh can be very large
            length = mesh2d->node[nr_mesh2d - 1]->name.size();
            length += mesh2d->node[nr_mesh2d - 1]->long_name.size();
            length += mesh2d->edge[nr_mesh2d - 1]->name.size();
            length += mesh2d->edge[nr_mesh2d - 1]->long_name.size();
            length += mesh2d->face[nr_mesh2d - 1]->name.size();
            length += mesh2d->face[nr_mesh2d - 1]->long_name.size();
            if (length != 0)
            {
#if defined NATIVE_C
                fprintf(stderr, "Length of the node/edge names of a 2D mesh have to be zero");
                return 1;
#else
#endif
            }
        }
    }

    return status;
}
//------------------------------------------------------------------------------
int UGRID::get_coordinate(char * mesh, char * location, int p, double * x, double * y)
{
    // find the mesh
    // find the location coordinates
    return -1;
}

int UGRID::read_grid_mapping(int i_var, string var_name, string grid_mapping_name)
{
    int status = -1;
    mapping->epsg = -1;

    status = get_attribute(this->ncid, i_var, "name", &mapping->name);
    status = get_attribute(this->ncid, i_var, "epsg", &mapping->epsg);
    mapping->grid_mapping_name = grid_mapping_name; //  == status = get_attribute(this->ncid, i_var, "grid_mapping_name", &map->grid_mapping_name);
    status = get_attribute(this->ncid, i_var, "longitude_of_prime_meridian", &mapping->longitude_of_prime_meridian);
    status = get_attribute(this->ncid, i_var, "semi_major_axis", &mapping->semi_major_axis);
    status = get_attribute(this->ncid, i_var, "semi_minor_axis", &mapping->semi_minor_axis);
    status = get_attribute(this->ncid, i_var, "inverse_flattening", &mapping->inverse_flattening);
    status = get_attribute(this->ncid, i_var, "epsg_code", &mapping->epsg_code);
    status = get_attribute(this->ncid, i_var, "value", &mapping->value);
    status = get_attribute(this->ncid, i_var, "projection_name", &mapping->projection_name);
    status = get_attribute(this->ncid, i_var, "wkt", &mapping->wkt);

    if (mapping->epsg == -1 && mapping->epsg_code.size() != 0)
    {
        vector<string> token = tokenize(mapping->epsg_code, ':');
        mapping->epsg = atoi(token[1].c_str());  // second token contains the plain EPSG code
    }

    return status;
}
int UGRID::read_composite_mesh_attributes(struct _mesh_contact_string * mesh_contact_strings, int i_var, string var_name)
{
    int status = -1;
    mesh_contact_strings->var_name = var_name;
    status = get_attribute(this->ncid, i_var, "meshes", &mesh_contact_strings->meshes);
    status = get_attribute(this->ncid, i_var, "mesh_contact", &mesh_contact_strings->mesh_contact);

    vector<string> token = tokenize(mesh_contact_strings->meshes, ' ');
    mesh_contact_strings->mesh_a = token[0];
    mesh_contact_strings->mesh_b = token[1];

    return status;
}
int UGRID::read_network_attributes(struct _ntw_string * ntw_strings, int i_var, string var_name, size_t topology_dimension)
{
    int status = 1;

    ntw_strings->var_name = var_name;
    ntw_strings->toplogy_dimension = topology_dimension;

    status = get_attribute(this->ncid, i_var, "edge_dimension", &ntw_strings->edge_dimension);
    //status = get_attribute(this->ncid, i_var, "node_dimension", &ntw_strings->node_dimension);
    status = get_attribute(this->ncid, i_var, "edge_geometry", &ntw_strings->edge_geometry);
    status = get_attribute(this->ncid, i_var, "edge_length", &ntw_strings->edge_length);
    if (status == NC_ENOTATT)
    {
        status = get_attribute(this->ncid, i_var, "branch_lengths", &ntw_strings->edge_length);
    }
    status = get_attribute(this->ncid, i_var, "edge_node_connectivity", &ntw_strings->edge_node_connectivity);
    status = get_attribute(this->ncid, i_var, "long_name", &ntw_strings->long_name);
    status = get_attribute(this->ncid, i_var, "node_coordinates", &ntw_strings->node_coordinates);
    //status = get_attribute(this->ncid, i_var, "node_edge_exchange", &ntw_string->node_edge_exchange);

    //get nodes (x_geom_name y_geom_name) of the network (ie connection nodes)
    // split the node_coordinates string into two separate strings (x_geom_name and y_geom_name)
    vector<string> token = tokenize(ntw_strings->node_coordinates, ' ');
    ntw_strings->x_ntw_name = token[0];
    ntw_strings->y_ntw_name = token[1];

    // Non ugrid standard attributes

    // ids and long_names of nodes and branches
    status = get_attribute(this->ncid, i_var, "node_id", &ntw_strings->node_names);
    if (status != NC_NOERR) { status = get_attribute(this->ncid, i_var, "node_ids", &ntw_strings->node_names); }
    status = get_attribute(this->ncid, i_var, "node_long_name", &ntw_strings->node_long_names);
    if (status != NC_NOERR) { status = get_attribute(this->ncid, i_var, "node_long_names", &ntw_strings->node_long_names); }
    status = get_attribute(this->ncid, i_var, "branch_id", &ntw_strings->edge_names);
    if (status != NC_NOERR) { status = get_attribute(this->ncid, i_var, "branch_ids", &ntw_strings->edge_names); }
    status = get_attribute(this->ncid, i_var, "branch_long_name", &ntw_strings->edge_long_names);
    if (status != NC_NOERR) { status = get_attribute(this->ncid, i_var, "branch_long_names", &ntw_strings->edge_long_names); }
    status = get_attribute(this->ncid, i_var, "branch_order", &ntw_strings->edge_order);

    return status;
}

int UGRID::read_geometry_attributes(struct _geom_string * geom_strings, int i_var, string var_name, int topology_dimension)
{
    int status = -1;
    geom_strings->var_name = var_name;

    status = get_attribute(this->ncid, i_var, "node_coordinates", &geom_strings->node_coordinates);

    status = get_attribute(this->ncid, i_var, "node_count", &geom_strings->node_dimension);
    status = get_attribute(this->ncid, i_var, "part_node_count", &geom_strings->node_count);
    if (status != NC_NOERR)
    {
        status = get_attribute(this->ncid, i_var, "node_dimension", &geom_strings->node_dimension);
        if (status != NC_NOERR)
        {
            geom_strings->node_dimension = strdup("nnetwork_geometry");
#ifdef NATIVE_C
            fprintf(stderr, "\tAttribute \'node_dimension\' not found, set to: %s\n", geom_strings->node_dimension.c_str());
#else
            //QMessageBox::warning(0, "Message", QString("UGRID::read_mesh()\nAttribute \'node_dimension\' not found, set to: %1\n").arg(ntw->geom_node_dimension));
#endif
        }
        status = get_attribute(this->ncid, i_var, "node_count", &geom_strings->node_count);
    }
    if (status != NC_NOERR)
    {
        geom_strings->node_dimension = strdup("nnetwork_geometry");
#ifdef NATIVE_C
        fprintf(stderr, "\tAttribute \'node_dimension\' not found, set to: %s\n", geom_strings->node_dimension.c_str());
#else
        //QMessageBox::warning(0, "Message", QString("UGRID::read_mesh()\nAttribute \'node_dimension\' not found, set to: %1\n").arg(ntw->geom_node_dimension));
#endif
    }

    // get geometry nodes (x_geom_name y_geom_name) of the network geometry (thalweg)
    // split the geom_node_coordinates string into two separate strings (x_geom_name and y_geom_name)
    vector<string> token = tokenize(geom_strings->node_coordinates, ' ');
    if (token.size() == 2)
    {
        geom_strings->x_geom_name = token[0];
        geom_strings->y_geom_name = token[1];
    }
    else
    {
        QMessageBox::critical(0, "Critical message", QString("UGRID::read_geometry_attributes\nFile does not contain the geometry coordinates attribute\n"));
        status = 1;
    }

    return status;
}

int UGRID::read_mesh1d_attributes(struct _mesh1d_string * mesh1d_strings, int i_var, string var_name, int topology_dimension)
{
    int status = 1;

    mesh1d_strings->var_name = var_name;
    mesh1d_strings->topology_dimension = size_t(topology_dimension);

    status = get_attribute(this->ncid, i_var, "coordinate_space", &mesh1d_strings->coordinate_space);
    status = get_attribute(this->ncid, i_var, "edge_dimension", &mesh1d_strings->edge_dimension);
    status = get_attribute(this->ncid, i_var, "node_dimension", &mesh1d_strings->node_dimension);
    //status = get_attribute(this->ncid, i_var, "edge_length", &mesh1d_strings->edge_length);  // is given by edge length between connection nodes
    status = get_attribute(this->ncid, i_var, "edge_node_connectivity", &mesh1d_strings->edge_node_connectivity);
    status = get_attribute(this->ncid, i_var, "long_name", &mesh1d_strings->long_name);
    status = get_attribute(this->ncid, i_var, "node_coordinates", &mesh1d_strings->node_coordinates);
    status = get_attribute(this->ncid, i_var, "edge_coordinates", &mesh1d_strings->edge_coordinates);  // optional required
    if (status != NC_NOERR)
    {
        mesh1d_strings->edge_coordinates = "";
    }
    status = get_attribute(this->ncid, i_var, "node_edge_exchange", &mesh1d_strings->node_edge_exchange);

    // split 'node coordinate' string
    vector<string> token = tokenize(mesh1d_strings->node_coordinates, ' ');
    int var_id;
    for (int i = 0; i < token.size(); i++)
    {
        var_id = -1;
        status = nc_inq_varid(this->ncid, token[i].c_str(), &var_id);
        string att_value;
        status = get_attribute(this->ncid, var_id, "standard_name", &att_value);
        if (status == NC_NOERR)
        {
            if (att_value == "projection_x_coordinate" || att_value == "longitude")
            {
                mesh1d_strings->x_node_name = token[i];
            }
            else if (att_value == "projection_y_coordinate" || att_value == "latitude")
            {
                mesh1d_strings->y_node_name = token[i];
            }
            else
            {
                status = get_attribute(this->ncid, var_id, "units", &att_value);  // does the units attribute exist?
                if (status == NC_NOERR)
                {
                    mesh1d_strings->node_chainage = token[i];
                }
                else
                {
                    mesh1d_strings->node_branch = token[i];
                }
            }
        }
        else  // no standard name found, so it can be a coordinate
        {
            status = get_attribute(this->ncid, var_id, "units", &att_value);  // does the attribute units exists?
            if (status == NC_NOERR)
            {
                mesh1d_strings->node_chainage = token[i];
            }
            else
            {
                mesh1d_strings->node_branch = token[i];
            }
        }
    }
    // split 'edge coordinate' string
    token = tokenize(mesh1d_strings->edge_coordinates, ' ');
    for (int i = 0; i < token.size(); i++)
    {
        var_id = -1;
        status = nc_inq_varid(this->ncid, token[i].c_str(), &var_id);
        string att_value;
        status = get_attribute(this->ncid, var_id, "standard_name", &att_value);
        if (status == NC_NOERR)
        {
            if (att_value == "projection_x_coordinate" || att_value == "longitude")
            {
                mesh1d_strings->x_edge_name = token[i];
            }
            else if (att_value == "projection_y_coordinate" || att_value == "latitude")
            {
                mesh1d_strings->y_edge_name = token[i];
            }
            else
            {
                char * att_value_c = (char *)malloc(sizeof(char) * (NC_MAX_NAME + 1));
                status = get_attribute(this->ncid, var_id, "units", &att_value_c);  // does the units attribute exist?
                if (status == NC_NOERR)
                {
                    mesh1d_strings->edge_chainage = token[i];
                }
                else
                {
                    mesh1d_strings->edge_branch = token[i];
                }
                free(att_value_c);
                att_value_c = nullptr;
            }
        }
        else  // no standard name found
        {
            char * att_value_c = (char *)malloc(sizeof(char) * (NC_MAX_NAME + 1));
            status = get_attribute(this->ncid, var_id, "units", &att_value_c);  // does the attribute units exists?
            if (status == NC_NOERR)
            {
                mesh1d_strings->edge_chainage = token[i];
            }
            else
            {
                mesh1d_strings->edge_branch = token[i];
            }
            free(att_value_c);
            att_value_c = nullptr;
        }
    }

    // ids and long_names of nodes and branches
    status = get_attribute(this->ncid, i_var, "node_id", &mesh1d_strings->node_names);
    if (status != NC_NOERR) { status = get_attribute(this->ncid, i_var, "node_ids", &mesh1d_strings->node_names); }
    status = get_attribute(this->ncid, i_var, "node_long_name", &mesh1d_strings->node_long_names);
    if (status != NC_NOERR) { status = get_attribute(this->ncid, i_var, "node_long_names", &mesh1d_strings->node_long_names); }
    status = get_attribute(this->ncid, i_var, "branch_id", &mesh1d_strings->edge_names);
    if (status != NC_NOERR) { status = get_attribute(this->ncid, i_var, "branch_ids", &mesh1d_strings->edge_names); }
    status = get_attribute(this->ncid, i_var, "branch_long_name", &mesh1d_strings->edge_long_names);
    if (status != NC_NOERR) { status = get_attribute(this->ncid, i_var, "branch_long_names", &mesh1d_strings->edge_long_names); }

    return status;
    }

int UGRID::read_mesh2d_attributes(struct _mesh2d_string * mesh2d_strings, int i_var, string var_name, int topology_dimension)
{
    int status = 1;

    mesh2d_strings->var_name = var_name;

    // Required attributes
    //
    // cf_role == mesh_topology
    // topology_dimension
    // node coordinates
    // face_node_connectivity
    mesh2d_strings->topology_dimension = size_t(topology_dimension);
    status = get_attribute(this->ncid, i_var, "node_coordinates", &mesh2d_strings->node_coordinates);
    if (status == NC_NOERR) { status = get_attribute(this->ncid, i_var, "face_node_connectivity", &mesh2d_strings->face_node_connectivity); }
    if (status != NC_NOERR)
    {
#ifdef NATIVE_C
        fprintf(stderr, "\tMesh \'%s\' does not meet the UGRID standard for 2D meshes. Required attributes are missing.\n", mesh2d_strings->var_name.c_str());
#else
        //QMessageBox::warning(0, "Message", QString("UGRID::read_mesh2d_attributes()\nMesh \'%1\"does not meet the UGRID standard\nRequired attributes are missing").arg(mesh2d_strings->var_name.c_str()));
#endif
    }

    // optional required attributes
    status = get_attribute(this->ncid, i_var, "edge_dimension", &mesh2d_strings->edge_dimension);
    status = get_attribute(this->ncid, i_var, "face_dimension", &mesh2d_strings->face_dimension);
    status = get_attribute(this->ncid, i_var, "edge_node_connectivity", &mesh2d_strings->edge_node_connectivity);

    // optional attributes
    //status = get_attribute(this->ncid, i_var, "coordinate_space", &mesh2d_strings[nr_mesh2d - 1]->coordinate_space);
    status = get_attribute(this->ncid, i_var, "edge_coordinates", &mesh2d_strings->edge_coordinates);
    status = get_attribute(this->ncid, i_var, "edge_geometry", &mesh2d_strings->edge_geometry);
    status = get_attribute(this->ncid, i_var, "edge_length", &mesh2d_strings->edge_length);
    status = get_attribute(this->ncid, i_var, "long_name", &mesh2d_strings->long_name);
    //status = get_attribute(this->ncid, i_var, "node_dimension", &mesh2d_strings->node_dimension);
    status = get_attribute(this->ncid, i_var, "node_edge_exchange", &mesh2d_strings->node_edge_exchange);

    status = get_attribute(this->ncid, i_var, "max_face_nodes_dimension", &mesh2d_strings->max_face_nodes_dimension);
    status = get_attribute(this->ncid, i_var, "edge_face_connectivity", &mesh2d_strings->edge_face_connectivity);
    status = get_attribute(this->ncid, i_var, "face_coordinates", &mesh2d_strings->face_coordinates);

    status = get_attribute(this->ncid, i_var, "layer_dimension", &mesh2d_strings->layer_dimension);
    if (status == NC_NOERR) { status = get_attribute(this->ncid, i_var, "interface_dimension", &mesh2d_strings->layer_interface_dimension); }
    if (status != NC_NOERR)
    {
        mesh2d_strings->layer_dimension = "";
        mesh2d_strings->layer_interface_dimension = "";
    }
    // split required 'node coordinate' string
    vector<string> token = tokenize(mesh2d_strings->node_coordinates, ' ');
    if (token.size() == 2)
    {
        mesh2d_strings->x_node_name = token[0];
        mesh2d_strings->y_node_name = token[1];
    }

    // split 'edge coordinate' string
    token = tokenize(mesh2d_strings->edge_coordinates, ' ');
    if (token.size() == 2)
    {
        mesh2d_strings->x_edge_name = token[0];
        mesh2d_strings->y_edge_name = token[1];
        status = get_attribute_by_var_name(this->ncid, mesh2d_strings->x_edge_name, "bounds", &mesh2d_strings->x_bound_edge_name);
        status = get_attribute_by_var_name(this->ncid, mesh2d_strings->y_edge_name, "bounds", &mesh2d_strings->y_bound_edge_name);
    }

    // split 'face_coordinates' string
    token = tokenize(mesh2d_strings->face_coordinates, ' ');
    if (token.size() == 2)
    {
        mesh2d_strings->x_face_name = token[0];
        mesh2d_strings->y_face_name = token[1];
        status = get_attribute_by_var_name(this->ncid, mesh2d_strings->x_face_name, "bounds", &mesh2d_strings->x_bound_face_name);
        status = get_attribute_by_var_name(this->ncid, mesh2d_strings->y_face_name, "bounds", &mesh2d_strings->y_bound_face_name);
    }

    return status;
}

int UGRID::create_mesh1d_nodes(struct _mesh1d * mesh1d, struct _ntw_edges * ntw_edges, struct _ntw_geom * ntw_geom)
{
    // determine the (x, y) location of the mesh1d node along the geometry of the network

    if (mesh1d == nullptr || ntw_edges == nullptr || ntw_geom == nullptr)
    {
        return 0;  // there is no 1D mesh or network
    }
    size_t nr_ntw = ntw_geom->nr_ntw;
    if (mesh1d->node[nr_ntw - 1]->branch.size() == 0)
    {
        return 0;  // Coordinates already read
    }

#ifndef NATIVE_C
    int pgbar_value = 950;
    m_pgBar->setValue(pgbar_value);
#endif

    int status = -1;
    for (int i_mesh1d = 1; i_mesh1d <= mesh1d->nr_mesh1d; i_mesh1d++)  // loop over 1D meshes
    {
        mesh1d->node[i_mesh1d - 1]->x = vector<double>(mesh1d->node[i_mesh1d - 1]->count);
        mesh1d->node[i_mesh1d - 1]->y = vector<double>(mesh1d->node[i_mesh1d - 1]->count);

        double xp;
        double yp;
        vector<double> chainage;
        for (int branch = 0; branch < ntw_geom->geom[nr_ntw - 1]->count; branch++)  // loop over the geometries
        {
            double branch_length = ntw_edges->edge[nr_ntw - 1]->edge_length[branch];
            size_t geom_nodes_count = ntw_geom->geom[nr_ntw - 1]->nodes[branch]->count;

            chainage.resize(geom_nodes_count);
            chainage[0] = 0.0;
            for (int i = 1; i < geom_nodes_count; i++)
            {
                double x1 = ntw_geom->geom[nr_ntw - 1]->nodes[branch]->x[i - 1];
                double y1 = ntw_geom->geom[nr_ntw - 1]->nodes[branch]->y[i - 1];
                double x2 = ntw_geom->geom[nr_ntw - 1]->nodes[branch]->x[i];
                double y2 = ntw_geom->geom[nr_ntw - 1]->nodes[branch]->y[i];

                chainage[i] = chainage[i - 1] + sqrt((x2 - x1)*(x2 - x1) + (y2 - y1)*(y2 - y1));  // todo HACK: this is just the euclidian distance
            }

            for (int i = 0; i <  mesh1d->node[i_mesh1d - 1]->count; i++)  // loop over computational nodes
            {
                if (mesh1d->node[nr_ntw - 1]->branch[i] == branch)  // is this node on this edge
                {
                    double fraction = -1.0;
                    double chainage_node = mesh1d->node[i_mesh1d - 1]->chainage[i];
                    fraction = chainage_node / branch_length;
                    if (fraction < 0.0 || fraction > 1.0)
                    {
#ifdef NATIVE_C
                        fprintf(stderr, "UGRID::determine_computational_node_on_geometry()\n\tBranch(%d). Offset %f is larger then branch length %f.\n", branch + 1, chainage_node, branch_length);
#else
                        //QMessageBox::warning(0, "Message", QString("UGRID::determine_computational_node_on_geometry()\nBranch(%3). Offset %1 is larger then branch length %2.\n").arg(offset).arg(branch_length).arg(branch+1));
#endif
                    }
                    double chainage_point = fraction * chainage[geom_nodes_count - 1];

                    xp = ntw_geom->geom[nr_ntw - 1]->nodes[branch]->x[0];
                    yp = ntw_geom->geom[nr_ntw - 1]->nodes[branch]->y[0];

                    for (int i = 1; i < geom_nodes_count; i++)
                    {
                        if (chainage_point <= chainage[i])
                        {
                            double alpha = (chainage_point - chainage[i - 1]) / (chainage[i] - chainage[i - 1]);
                            double x1 = ntw_geom->geom[nr_ntw - 1]->nodes[branch]->x[i - 1];
                            double y1 = ntw_geom->geom[nr_ntw - 1]->nodes[branch]->y[i - 1];
                            double x2 = ntw_geom->geom[nr_ntw - 1]->nodes[branch]->x[i];
                            double y2 = ntw_geom->geom[nr_ntw - 1]->nodes[branch]->y[i];

                            xp = x1 + alpha * (x2 - x1);
                            yp = y1 + alpha * (y2 - y1);
                            break;
                        }
                    }
                    mesh1d->node[i_mesh1d - 1]->x[i] = xp;
                    mesh1d->node[i_mesh1d - 1]->y[i] = yp;
                }
            }
        }
        status = 0;
    }
#ifndef NATIVE_C
    m_pgBar->setValue(990);
#endif
    return status;
}
int UGRID::create_mesh_contacts(struct _ntw_nodes * mesh1d_nodes, struct _ntw_edges * ntw_edges, struct _ntw_geom * ntw_geom)
{
    // zoek variabele mesh_a
    // haal bijbehorende (x, y) coordinate van beginpunt van de edge op
    // zoek variabele mesh_b
    // haal bijbehorende (x, y) coordinate van het eindpunt van de edge op

    int k = 0;
    for (int i = 0; i <mesh_contact->edge[m_nr_mesh_contacts - 1]->count; i++)
    {
        // p1: begin point edge
        int p1 = mesh_contact->edge[m_nr_mesh_contacts - 1]->edge_nodes[i][0];
        mesh_contact->node[0]->x[i] = mesh1d_nodes->node[0]->x[p1];
        mesh_contact->node[0]->y[i] = mesh1d_nodes->node[0]->y[p1];

        // p2: end point edge
        int p2 = mesh_contact->edge[m_nr_mesh_contacts - 1]->edge_nodes[i][1];
        k = 2 * i + 1;
        //mesh_contact->node[0]->x[k] = mesh2d->face[0]->x[p2];
        //mesh_contact->node[0]->y[k] = mesh2d->face[0]->y[p2];

        //status = get_coordinate(mesh_contact->mesh_a, mesh_contact->location_a, mesh_contact->edge[m_nr_mesh_contacts - 1]->edge_nodes[i][0], x1, y1);
        //status = get_coordinate_y(mesh_contact->mesh_a, mesh_contact->location_a, mesh_contact->edge[m_nr_mesh_contacts - 1]->);
    }
    return 0;

}

char * UGRID::strndup(const char *s, size_t n)
{
    char *result;
    size_t len = strlen(s);

    if (n < len)
        len = n;

    result = (char *)malloc(len + 1);
    if (!result)
        return 0;

    result[len] = '\0';
    return (char *)memcpy(result, s, len);
}

std::vector<std::string> UGRID::tokenize(const std::string& s, char c) {
    auto end = s.cend();
    auto start = end;

    std::vector<std::string> v;
    for (auto it = s.cbegin(); it != end; ++it) {
        if (*it != c) {
            if (start == end)
                start = it;
            continue;
        }
        if (start != end) {
            v.emplace_back(start, it);
            start = end;
        }
    }
    if (start != end)
        v.emplace_back(start, end);
    return v;
}

std::vector<std::string> UGRID::tokenize(const std::string& s, std::size_t count)
{
    size_t minsize = s.size() / count;
    std::vector<std::string> tokens;
    for (size_t i = 0, offset = 0; i < count; ++i)
    {
        size_t size = minsize;
        if ((offset + size) < s.size())
            tokens.push_back(s.substr(offset, size));
        else
            tokens.push_back(s.substr(offset, s.size() - offset));
        offset += size;
    }
    return tokens;
}
