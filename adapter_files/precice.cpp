/*!
 * \file precice.cpp
 * \brief Adapter class for coupling SU2 with preCICE for FSI.
 * \author Alexander Rusch
 */

#include "../include/precice.hpp"
// #include "../../Common/include/containers/container_decorators.hpp"

Precice::Precice(const string& preciceConfigurationFileName, const std::string& preciceParticipantName,
                 const std::string& preciceReadDataName_, const std::string& preciceWriteDataName_,
                 const std::string& preciceMeshName_, int solverProcessIndex, int solverProcessSize,
                 CGeometry**** geometry_container, CSolver***** solver_container, CConfig** config_container,
                 CVolumetricMovement*** grid_movement)
    : solverProcessIndex(solverProcessIndex),
      solverProcessSize(solverProcessSize),
      solverInterface(preciceParticipantName, preciceConfigurationFileName, solverProcessIndex, solverProcessSize),
      nDim(geometry_container[ZONE_0][INST_0][MESH_0]->GetnDim()),
      geometry_container(geometry_container),
      solver_container(solver_container),
      config_container(config_container),
      grid_movement(grid_movement),
      vertexIDs(NULL),
      forceID(NULL),
      heatFluxID(NULL),
      displDeltaID(NULL),
      tempID(NULL),
      forces(NULL),
      heatFluxes(NULL),
      displacements(NULL),
      temperatures(NULL),
      displacements_n(NULL),
      displacementDeltas(NULL),
      // For implicit coupling
      coric(precice::constants::actionReadIterationCheckpoint()),
      cowic(precice::constants::actionWriteIterationCheckpoint()),
      processWorkingOnWetSurface(true),
      verbosityLevel_high(config_container[ZONE_0]->GetpreCICE_VerbosityLevel_High()),
      globalNumberWetSurfaces(config_container[ZONE_0]->GetpreCICE_NumberWetSurfaces()),
      localNumberWetSurfaces(0),
      // Get value (= index) of the marker corresponding to the wet surface
      // It is implied, that only one marker is used for the entire wet surface, even if it is split into parts
      valueMarkerWet(NULL),
      vertexSize(NULL),
      indexMarkerWetMappingLocalToGlobal(NULL),
      preciceReadDataName(preciceReadDataName_),
      preciceWriteDataName(preciceWriteDataName_),
      preciceMeshName(preciceMeshName_),
      // Variables for implicit coupling
      nPoint(geometry_container[ZONE_0][INST_0][MESH_0]->GetnPoint()),
      nVar(solver_container[ZONE_0][INST_0][MESH_0][FLOW_SOL]->GetnVar()),
      Coord_Saved(NULL),
      Coord_n_Saved(NULL),
      Coord_n1_Saved(NULL),
      Coord_p1_Saved(NULL),
      GridVel_Saved(NULL),
      GridVel_Grad_Saved(NULL),
      dt_savedState(0),
      StopCalc_savedState(false),
      solution_Saved(NULL),
      solution_time_n_Saved(NULL),
      solution_time_n1_Saved(NULL) {
  if (preciceReadDataName.find("Temperature") == std::string::npos)
    if (preciceReadDataName.find("Delta") == std::string::npos)
      readDataType = ReadDataType::Displacement;
    else
      readDataType = ReadDataType::DisplacementDelta;
  else
    readDataType = ReadDataType::Temperature;

  Coord_Saved = new double*[nPoint];
  Coord_n_Saved = new double*[nPoint];
  Coord_n1_Saved = new double*[nPoint];
  Coord_p1_Saved = new double*[nPoint];
  GridVel_Saved = new double*[nPoint];
  GridVel_Grad_Saved = new double**[nPoint];
  solution_Saved = new double*[nPoint];
  solution_time_n_Saved = new double*[nPoint];
  solution_time_n1_Saved = new double*[nPoint];
  for (int iPoint = 0; iPoint < nPoint; iPoint++) {
    Coord_Saved[iPoint] = new double[nDim];
    Coord_n_Saved[iPoint] = new double[nDim];
    Coord_n1_Saved[iPoint] = new double[nDim];
    Coord_p1_Saved[iPoint] = new double[nDim];
    GridVel_Saved[iPoint] = new double[nDim];
    GridVel_Grad_Saved[iPoint] = new double*[nDim];
    for (int iDim = 0; iDim < nDim; iDim++) {
      GridVel_Grad_Saved[iPoint][iDim] = new double[nDim];
    }
    solution_Saved[iPoint] = new double[nVar];
    solution_time_n_Saved[iPoint] = new double[nVar];
    solution_time_n1_Saved[iPoint] = new double[nVar];
  }
}

Precice::~Precice(void) {
  for (int i = 0; i < localNumberWetSurfaces; i++) {
    if (vertexIDs[i] != NULL) {
      delete[] vertexIDs[i];
    }
  }
  if (vertexIDs != NULL) {
    delete[] vertexIDs;
  }
  if (forceID != NULL) {
    delete[] forceID;
  }
  if (heatFluxID != NULL) {
    delete[] heatFluxID;
  }
  if (displDeltaID != NULL) {
    delete[] displDeltaID;
  }
  if (tempID != NULL) {
    delete[] tempID;
  }
  if (valueMarkerWet != NULL) {
    delete[] valueMarkerWet;
  }
  if (vertexSize != NULL) {
    delete[] vertexSize;
  }
  if (indexMarkerWetMappingLocalToGlobal != NULL) {
    delete[] indexMarkerWetMappingLocalToGlobal;
  }

  for (int iPoint = 0; iPoint < nPoint; iPoint++) {
    if (Coord_Saved[iPoint] != NULL) {
      delete[] Coord_Saved[iPoint];
    }
    if (Coord_n_Saved[iPoint] != NULL) {
      delete[] Coord_n_Saved[iPoint];
    }
    if (Coord_n1_Saved[iPoint] != NULL) {
      delete[] Coord_n1_Saved[iPoint];
    }
    if (Coord_p1_Saved[iPoint] != NULL) {
      delete[] Coord_p1_Saved[iPoint];
    }
    if (GridVel_Saved[iPoint] != NULL) {
      delete[] GridVel_Saved[iPoint];
    }
    for (int iDim = 0; iDim < nDim; iDim++) {
      if (GridVel_Grad_Saved[iPoint][iDim] != NULL) {
        delete[] GridVel_Grad_Saved[iPoint][iDim];
      }
    }
    if (GridVel_Grad_Saved[iPoint] != NULL) {
      delete[] GridVel_Grad_Saved[iPoint];
    }
    if (solution_Saved[iPoint] != NULL) {
      delete[] solution_Saved[iPoint];
    }
    if (solution_time_n_Saved[iPoint] != NULL) {
      delete[] solution_time_n_Saved[iPoint];
    }
    if (solution_time_n1_Saved[iPoint] != NULL) {
      delete[] solution_time_n1_Saved[iPoint];
    }
  }
  if (Coord_Saved != NULL) {
    delete[] Coord_Saved;
  }
  if (Coord_n_Saved != NULL) {
    delete[] Coord_n_Saved;
  }
  if (Coord_n1_Saved != NULL) {
    delete[] Coord_n1_Saved;
  }
  if (Coord_p1_Saved != NULL) {
    delete[] Coord_p1_Saved;
  }
  if (GridVel_Saved != NULL) {
    delete[] GridVel_Saved;
  }
  if (GridVel_Grad_Saved != NULL) {
    delete[] GridVel_Grad_Saved;
  }
  if (solution_Saved != NULL) {
    delete[] solution_Saved;
  }
  if (solution_time_n_Saved != NULL) {
    delete[] solution_time_n_Saved;
  }
  if (solution_time_n1_Saved != NULL) {
    delete[] solution_time_n1_Saved;
  }
  if (displacements_n != NULL) {
    delete[] displacements_n;
  }
}

double Precice::initialize() {
  if (verbosityLevel_high) {
    cout << "Process #" << solverProcessIndex << "/" << solverProcessSize - 1 << ": Initializing preCICE..." << endl;
  }

  // Checking for dimensional consistency of SU2 and preCICE - Exit if not consistent
  if (solverInterface.getDimensions() != geometry_container[ZONE_0][INST_0][MESH_0]->GetnDim()) {
    cout << "Dimensions of SU2 and preCICE are not equal! Now exiting..." << endl;
    exit(EXIT_FAILURE);
  }
  int* meshID;
  // Checking for number of wet surfaces - Exit if not cat least one wet surface defined
  if (globalNumberWetSurfaces < 1) {
    cout << "There must be at least one wet surface! Now exiting..." << endl;
    exit(EXIT_FAILURE);
  } else {
    meshID = new int[globalNumberWetSurfaces];
    forceID = new int[globalNumberWetSurfaces];
    displDeltaID = new int[globalNumberWetSurfaces];
    tempID = new int[globalNumberWetSurfaces];
    heatFluxID = new int[globalNumberWetSurfaces];
    for (int i = 0; i < globalNumberWetSurfaces; i++) {
      // Get preCICE meshIDs
      meshID[i] = solverInterface.getMeshID(preciceMeshName + (i == 0 ? "" : to_string(i)));
    }
  }

  // Determine the number of wet surfaces, that this process is working on, then loop over this number for all
  // respective preCICE-related tasks
  for (int i = 0; i < globalNumberWetSurfaces; i++) {
    if (config_container[ZONE_0]->GetMarker_All_TagBound(config_container[ZONE_0]->GetpreCICE_WetSurfaceMarkerName() +
                                                         (i == 0 ? "" : to_string(i))) == -1) {
      cout << "Process #" << solverProcessIndex << "/" << solverProcessSize - 1 << ": Does not work on "
           << config_container[ZONE_0]->GetpreCICE_WetSurfaceMarkerName() << (i == 0 ? "" : to_string(i)) << endl;
    } else {
      localNumberWetSurfaces++;
    }
  }
  if (localNumberWetSurfaces < 1) {
    cout << "Process #" << solverProcessIndex << "/" << solverProcessSize - 1
         << ": Does not work on the wet surface at all." << endl;
    processWorkingOnWetSurface = false;
  }

  if (processWorkingOnWetSurface) {
    // Store the wet surface marker values in an array, which has the size equal to the number of wet surfaces actually
    // being worked on by this process
    valueMarkerWet = new short[localNumberWetSurfaces];
    indexMarkerWetMappingLocalToGlobal = new short[localNumberWetSurfaces];
    int j = 0;
    for (int i = 0; i < globalNumberWetSurfaces; i++) {
      if (config_container[ZONE_0]->GetMarker_All_TagBound(config_container[ZONE_0]->GetpreCICE_WetSurfaceMarkerName() +
                                                           (i == 0 ? "" : to_string(i))) != -1) {
        valueMarkerWet[j] = config_container[ZONE_0]->GetMarker_All_TagBound(
            config_container[ZONE_0]->GetpreCICE_WetSurfaceMarkerName() + (i == 0 ? "" : to_string(i)));
        indexMarkerWetMappingLocalToGlobal[j] = i;
        j++;
      }
    }
    vertexIDs = new int*[localNumberWetSurfaces];
  }

  if (processWorkingOnWetSurface) {
    vertexSize = new unsigned long[localNumberWetSurfaces];
    for (int i = 0; i < localNumberWetSurfaces; i++) {
      vertexSize[i] = geometry_container[ZONE_0][INST_0][MESH_0]->nVertex[valueMarkerWet[i]];

      double coupleNodeCoord[vertexSize[i]][nDim]; /*--- coordinates of all nodes at the wet surface ---*/

      unsigned long iNode; /*--- variable for storing the node indices - one at the time ---*/
      // Loop over the vertices of the (each) boundary
      for (int iVertex = 0; iVertex < vertexSize[i]; iVertex++) {
        // Get node number (= index) to vertex (= node)
        iNode = geometry_container[ZONE_0][INST_0][MESH_0]->vertex[valueMarkerWet[i]][iVertex]->GetNode();

        // Get coordinates for nodes
        for (int iDim = 0; iDim < nDim; iDim++) {
          coupleNodeCoord[iVertex][iDim] = geometry_container[ZONE_0][INST_0][MESH_0]->nodes->GetCoord(iNode, iDim);
          if (verbosityLevel_high) {
            cout << "Process #" << solverProcessIndex << "/" << solverProcessSize - 1
                 << ": Initial coordinates of node (local index, global index, node color): (" << iVertex << ", "
                 << iNode << ", " << geometry_container[ZONE_0][INST_0][MESH_0]->nodes->GetColor(iNode)
                 << "): " << coupleNodeCoord[iVertex][iDim] << endl; /*--- for debugging purposes ---*/
          }
        }
      }
      // preCICE conform the coordinates of vertices (= points = nodes) at wet surface
      double coords[vertexSize[i] * nDim];
      for (int iVertex = 0; iVertex < vertexSize[i]; iVertex++) {
        for (int iDim = 0; iDim < nDim; iDim++) {
          coords[iVertex * nDim + iDim] = coupleNodeCoord[iVertex][iDim];
        }
      }

      // preCICE internal
      vertexIDs[i] = new int[vertexSize[i]];

      solverInterface.setMeshVertices(meshID[indexMarkerWetMappingLocalToGlobal[i]], vertexSize[i], coords,
                                      vertexIDs[i]);

      if (readDataType != ReadDataType::Temperature) {
        forceID[indexMarkerWetMappingLocalToGlobal[i]] = solverInterface.getDataID(
            preciceWriteDataName +
                (indexMarkerWetMappingLocalToGlobal[i] == 0 ? "" : to_string(indexMarkerWetMappingLocalToGlobal[i])),
            meshID[indexMarkerWetMappingLocalToGlobal[i]]);
        displDeltaID[indexMarkerWetMappingLocalToGlobal[i]] = solverInterface.getDataID(
            preciceReadDataName +
                (indexMarkerWetMappingLocalToGlobal[i] == 0 ? "" : to_string(indexMarkerWetMappingLocalToGlobal[i])),
            meshID[indexMarkerWetMappingLocalToGlobal[i]]);

        if (readDataType == ReadDataType::Displacement) {
          displacements_n = new double[vertexSize[i] * nDim];
          for (int iVertex = 0; iVertex < vertexSize[i]; iVertex++) {
            for (int iDim = 0; iDim < nDim; iDim++) {
              displacements_n[iVertex * nDim + iDim] = 0;  // Init with zeros
            }
          }
        }

      } else {  // Else get CHT data IDs
        heatFluxID[indexMarkerWetMappingLocalToGlobal[i]] = solverInterface.getDataID(
            preciceWriteDataName +
                (indexMarkerWetMappingLocalToGlobal[i] == 0 ? "" : to_string(indexMarkerWetMappingLocalToGlobal[i])),
            meshID[indexMarkerWetMappingLocalToGlobal[i]]);

        tempID[indexMarkerWetMappingLocalToGlobal[i]] = solverInterface.getDataID(
            preciceReadDataName +
                (indexMarkerWetMappingLocalToGlobal[i] == 0 ? "" : to_string(indexMarkerWetMappingLocalToGlobal[i])),
            meshID[indexMarkerWetMappingLocalToGlobal[i]]);
      }
    }
    for (int i = 0; i < globalNumberWetSurfaces; i++) {
      bool flag = false;
      for (int j = 0; j < localNumberWetSurfaces; j++) {
        if (indexMarkerWetMappingLocalToGlobal[j] == i) {
          flag = true;
        }
      }
      if (!flag) {
        solverInterface.setMeshVertices(meshID[i], 0, NULL, NULL);
        if (readDataType != ReadDataType::Temperature) {
          forceID[i] = solverInterface.getDataID(preciceWriteDataName + (i == 0 ? "" : to_string(i)), meshID[i]);
          displDeltaID[i] = solverInterface.getDataID(preciceReadDataName + (i == 0 ? "" : to_string(i)), meshID[i]);
        } else {
          heatFluxID[i] = solverInterface.getDataID(preciceWriteDataName + (i == 0 ? "" : to_string(i)), meshID[i]);
          tempID[i] = solverInterface.getDataID(preciceReadDataName + (i == 0 ? "" : to_string(i)), meshID[i]);
        }
      }
    }
  } else {
    for (int i = 0; i < globalNumberWetSurfaces; i++) {
      solverInterface.setMeshVertices(meshID[i], 0, NULL, NULL);
      if (readDataType != ReadDataType::Temperature) {
        forceID[i] = solverInterface.getDataID(preciceWriteDataName + (i == 0 ? "" : to_string(i)), meshID[i]);
        displDeltaID[i] = solverInterface.getDataID(preciceReadDataName + (i == 0 ? "" : to_string(i)), meshID[i]);
      } else {
        heatFluxID[i] = solverInterface.getDataID(preciceWriteDataName + (i == 0 ? "" : to_string(i)), meshID[i]);
        tempID[i] = solverInterface.getDataID(preciceReadDataName + (i == 0 ? "" : to_string(i)), meshID[i]);
      }
    }
  }

  if (verbosityLevel_high and readDataType != ReadDataType::Temperature) {
    cout << "Process #" << solverProcessIndex << "/" << solverProcessSize - 1
         << ": There is grid movement (expected: 1): " << config_container[ZONE_0]->GetGrid_Movement()
         << endl; /*--- for debugging purposes ---*/
    /* No longer relevant: SU2 differentiates between a surface movement and grid movement now - no kind of grid
movement occurring cout << "Process #" << solverProcessIndex << "/" << solverProcessSize - 1
     << ": Kind of grid movement (expected: 13): " << config_container[ZONE_0]->GetKind_GridMovement()
     << endl;
             --- for debugging purposes ---*/
  }

  double precice_dt; /*--- preCICE timestep size ---*/
  precice_dt = solverInterface.initialize();
  if (verbosityLevel_high) {
    cout << "Process #" << solverProcessIndex << "/" << solverProcessSize - 1 << ": ...done initializing preCICE!"
         << endl;
  }
  if (meshID != NULL) {
    delete[] meshID;
  }
  return precice_dt;
}

double Precice::advance(double computedTimestepLength) {
  if (processWorkingOnWetSurface) {
    if (verbosityLevel_high) {
      cout << "Process #" << solverProcessIndex << "/" << solverProcessSize - 1 << ": Advancing preCICE..." << endl;
    }

    double factor = 0;

    string readDataString = config_container[ZONE_0]->GetpreCICE_ReadDataName();
    string writeDataString = config_container[ZONE_0]->GetpreCICE_WriteDataName();

    bool incompressible = false;
    bool viscous_flow = false;
    if (readDataType != ReadDataType::Temperature) {
      // Get physical simulation information
      incompressible = (config_container[ZONE_0]->GetKind_Regime() == ENUM_REGIME::INCOMPRESSIBLE);
      viscous_flow = ((config_container[ZONE_0]->GetKind_Solver() == MAIN_SOLVER::NAVIER_STOKES) ||
                      (config_container[ZONE_0]->GetKind_Solver() == MAIN_SOLVER::RANS));

      // Compute factor for redimensionalizing forces ("ND" = Non-Dimensional)
      double* Velocity_Real = config_container[ZONE_0]->GetVelocity_FreeStream();
      double Density_Real = config_container[ZONE_0]->GetDensity_FreeStream();
      double* Velocity_ND = config_container[ZONE_0]->GetVelocity_FreeStreamND();
      double Density_ND = config_container[ZONE_0]->GetDensity_FreeStreamND();
      double Velocity2_Real = 0.0; /*--- denotes squared real velocity ---*/
      double Velocity2_ND = 0.0;   /*--- denotes squared non-dimensional velocity ---*/
      // Compute squared values
      for (int iDim = 0; iDim < nDim; iDim++) {
        Velocity2_Real += Velocity_Real[iDim] * Velocity_Real[iDim];
        Velocity2_ND += Velocity_ND[iDim] * Velocity_ND[iDim];
      }
      // Compute factor for redimensionalizing forces
      factor = Density_Real * Velocity2_Real / (Density_ND * Velocity2_ND);

    } else {  // Else doing CHT - get factor for redimensionalizing heat flux

      factor = config_container[ZONE_0]->GetHeat_Flux_Ref();
    }

    if (verbosityLevel_high) {
      cout << "Process #" << solverProcessIndex << "/" << solverProcessSize - 1
           << ": Factor for (non-/re-)dimensionalization of " << writeDataString << ": " << factor
           << endl; /*--- for debugging purposes ---*/
    }

    for (int i = 0; i < localNumberWetSurfaces; i++) {
      unsigned long nodeVertex[vertexSize[i]];

      // 1. Compute forces/heat fluxes
      if (verbosityLevel_high) {
        cout << "Process #" << solverProcessIndex << "/" << solverProcessSize - 1
             << ": Advancing preCICE: Computing/Retrieving " << writeDataString << "s for "
             << config_container[ZONE_0]->GetpreCICE_WetSurfaceMarkerName() << indexMarkerWetMappingLocalToGlobal[i]
             << "..." << endl;
      }
      // This entire section can probably be rewritten with much simpler code as per SU2v7.4.0
      if (readDataType != ReadDataType::Temperature) {
        // Some variables to be used:
        unsigned long nodeVertex[vertexSize[i]];
        double normalsVertex[vertexSize[i]][nDim];
        double normalsVertex_Unit[vertexSize[i]][nDim];
        double Area;
        double Pn = 0.0;   /*--- denotes pressure at a node ---*/
        double Pinf = 0.0; /*--- denotes environmental (farfield) pressure ---*/
        // double** Grad_PrimVar =
        CMatrixView<double> Grad_PrimVar =
            NULL; /*--- denotes (u.A. velocity) gradients needed for computation of viscous forces ---*/
        double Viscosity = 0.0;
        double Tau[3][3];
        double TauElem[3];
        double forces_su2[vertexSize[i]]
                         [nDim]; /*--- forces will be stored such, before converting to simple array ---*/

        /*--- Loop over vertices of coupled boundary ---*/
        for (int iVertex = 0; iVertex < vertexSize[i]; iVertex++) {
          // Get node number (= index) to vertex (= node)
          nodeVertex[iVertex] = geometry_container[ZONE_0][INST_0][MESH_0]
                                    ->vertex[valueMarkerWet[i]][iVertex]
                                    ->GetNode(); /*--- Store all nodes (indices) in a vector ---*/
          // Get normal vector
          for (int iDim = 0; iDim < nDim; iDim++) {
            normalsVertex[iVertex][iDim] =
                (geometry_container[ZONE_0][INST_0][MESH_0]->vertex[valueMarkerWet[i]][iVertex]->GetNormal())[iDim];
          }
          // Unit normals
          Area = 0.0;
          for (int iDim = 0; iDim < nDim; iDim++) {
            Area += normalsVertex[iVertex][iDim] * normalsVertex[iVertex][iDim];
          }
          Area = sqrt(Area);
          for (int iDim = 0; iDim < nDim; iDim++) {
            normalsVertex_Unit[iVertex][iDim] = normalsVertex[iVertex][iDim] / Area;
          }
          // Get the values of pressure and viscosity
          Pn = solver_container[ZONE_0][INST_0][MESH_0][FLOW_SOL]->GetNodes()->GetPressure(nodeVertex[iVertex]);
          Pinf = solver_container[ZONE_0][INST_0][MESH_0][FLOW_SOL]->GetPressure_Inf();
          if (viscous_flow) {
            Grad_PrimVar = solver_container[ZONE_0][INST_0][MESH_0][FLOW_SOL]
                               ->GetNodes()
                               ->GetGradient_Primitive()[nodeVertex[iVertex]];
            Viscosity = solver_container[ZONE_0][INST_0][MESH_0][FLOW_SOL]->GetNodes()->GetLaminarViscosity(
                nodeVertex[iVertex]);
          }

          // Calculate the forces_su2 in the nodes for the inviscid term --> Units of force (non-dimensional).
          for (int iDim = 0; iDim < nDim; iDim++) {
            forces_su2[iVertex][iDim] = -(Pn - Pinf) * normalsVertex[iVertex][iDim];
          }
          // Calculate the forces_su2 in the nodes for the viscous term
          if (viscous_flow) {
            // Divergence of the velocity
            double div_vel = 0.0;
            for (int iDim = 0; iDim < nDim; iDim++) {
              div_vel += Grad_PrimVar[iDim + 1][iDim];
            }
            if (incompressible) {
              div_vel = 0.0; /*--- incompressible flow is divergence-free ---*/
            }
            for (int iDim = 0; iDim < nDim; iDim++) {
              for (int jDim = 0; jDim < nDim; jDim++) {
                // Dirac delta
                double Delta = 0.0;
                if (iDim == jDim) {
                  Delta = 1.0;
                }
                // Viscous stress
                Tau[iDim][jDim] = Viscosity * (Grad_PrimVar[jDim + 1][iDim] + Grad_PrimVar[iDim + 1][jDim]) -
                                  2 / 3 * Viscosity * div_vel * Delta;
                // Add Viscous component in the forces_su2 vector --> Units of force (non-dimensional).
                forces_su2[iVertex][iDim] += Tau[iDim][jDim] * normalsVertex[iVertex][jDim];
              }
            }
          }
          // Rescale forces_su2 to SI units
          for (int iDim = 0; iDim < nDim; iDim++) {
            forces_su2[iVertex][iDim] = forces_su2[iVertex][iDim] * factor;
          }
        }
        // convert forces_su2 into forces
        forces = new double[vertexSize[i] * nDim];
        for (int iVertex = 0; iVertex < vertexSize[i]; iVertex++) {
          for (int iDim = 0; iDim < nDim; iDim++) {
            // Do not write forces for duplicate nodes! -> Check wether the color of the node matches the MPI-rank of
            // this process. Only write forces, if node originally belongs to this process.
            if (geometry_container[ZONE_0][INST_0][MESH_0]->nodes->GetColor(nodeVertex[iVertex]) ==
                solverProcessIndex) {
              forces[iVertex * nDim + iDim] = forces_su2[iVertex][iDim];
            } else {
              forces[iVertex * nDim + iDim] = 0;
            }
          }
        }

      } else {  // Else we are doing CHT!

        // Get heat flux from solver container
        // As from CFlowOutput::LoadSurfaceData, ensure correct retrieval of HeatFlux
        const auto heat_sol = (config_container[ZONE_0]->GetKind_Regime() == ENUM_REGIME::INCOMPRESSIBLE) &&
                                      config_container[ZONE_0]->GetWeakly_Coupled_Heat()
                                  ? HEAT_SOL
                                  : FLOW_SOL;

        heatFluxes = new double[vertexSize[i]];  // Only one wall normal heat flux value to be sent!

        /*--- Loop over vertices of coupled boundary ---*/
        for (int iVertex = 0; iVertex < vertexSize[i]; iVertex++) {
          // Get node number (= index) to vertex (= node)
          nodeVertex[iVertex] = geometry_container[ZONE_0][INST_0][MESH_0]
                                    ->vertex[valueMarkerWet[i]][iVertex]
                                    ->GetNode(); /*--- Store all nodes (indices) in a vector ---*/

          if (geometry_container[ZONE_0][INST_0][MESH_0]->nodes->GetColor(nodeVertex[iVertex]) == solverProcessIndex) {
            heatFluxes[iVertex] =
                factor * solver_container[ZONE_0][INST_0][MESH_0][heat_sol]->GetHeatFlux(valueMarkerWet[i], iVertex);

          } else {
            heatFluxes[iVertex] = 0;
          }
        }
        if (verbosityLevel_high) {
          cout << "Process #" << solverProcessIndex << "/" << solverProcessSize - 1
               << ": Advancing preCICE: ...done retrieving heat fluxes for "
               << config_container[ZONE_0]->GetpreCICE_WetSurfaceMarkerName() << indexMarkerWetMappingLocalToGlobal[i]
               << endl;
        }
      }

      if (verbosityLevel_high) {
        cout << "Process #" << solverProcessIndex << "/" << solverProcessSize - 1
             << ": Advancing preCICE: ...done computing/retrieving " << writeDataString << "s for "
             << config_container[ZONE_0]->GetpreCICE_WetSurfaceMarkerName() << indexMarkerWetMappingLocalToGlobal[i]
             << endl;
      }

      // 2. Write forces/heat fluxes
      if (verbosityLevel_high) {
        cout << "Process #" << solverProcessIndex << "/" << solverProcessSize - 1 << ": Advancing preCICE: Writing "
             << writeDataString << "s for " << config_container[ZONE_0]->GetpreCICE_WetSurfaceMarkerName()
             << indexMarkerWetMappingLocalToGlobal[i] << "..." << endl;
      }

      if (readDataType != ReadDataType::Temperature) {
        solverInterface.writeBlockVectorData(forceID[indexMarkerWetMappingLocalToGlobal[i]], vertexSize[i],
                                             vertexIDs[i], forces);
      } else {
        solverInterface.writeBlockScalarData(heatFluxID[indexMarkerWetMappingLocalToGlobal[i]], vertexSize[i],
                                             vertexIDs[i], heatFluxes);
      }

      if (verbosityLevel_high) {
        cout << "Process #" << solverProcessIndex << "/" << solverProcessSize - 1
             << ": Advancing preCICE: ...done writing " << writeDataString << "s for "
             << config_container[ZONE_0]->GetpreCICE_WetSurfaceMarkerName() << indexMarkerWetMappingLocalToGlobal[i]
             << "." << endl;
      }
      if (forces != NULL) {
        delete[] forces;
      }
      if (heatFluxes != NULL) {
        delete[] heatFluxes;
      }
    }

    // 3. Advance solverInterface
    if (verbosityLevel_high) {
      cout << "Process #" << solverProcessIndex << "/" << solverProcessSize - 1
           << ": Advancing preCICE: Advancing SolverInterface..." << endl;
    }
    double max_precice_dt;
    max_precice_dt = solverInterface.advance(computedTimestepLength);
    if (verbosityLevel_high) {
      cout << "Process #" << solverProcessIndex << "/" << solverProcessSize - 1
           << ": Advancing preCICE: ...done advancing SolverInterface." << endl;
    }

    // displacements = new double[vertexSize*nDim]; //TODO: Delete later
    for (int i = 0; i < localNumberWetSurfaces; i++) {
      // 4. Read displacements/displacementDeltas/Temperatures
      if (verbosityLevel_high) {
        cout << "Process #" << solverProcessIndex << "/" << solverProcessSize - 1 << ": Advancing preCICE: Reading "
             << readDataString << "s for " << config_container[ZONE_0]->GetpreCICE_WetSurfaceMarkerName()
             << indexMarkerWetMappingLocalToGlobal[i] << "..." << endl;
      }
      double displacementDeltas_su2[vertexSize[i]][nDim]; /*--- displacementDeltas will be stored such, before
                                                             converting to simple array ---*/

      switch (readDataType) {
        case ReadDataType::DisplacementDelta: {
          displacementDeltas = new double[vertexSize[i] * nDim];
          solverInterface.readBlockVectorData(displDeltaID[indexMarkerWetMappingLocalToGlobal[i]], vertexSize[i],
                                              vertexIDs[i], displacementDeltas);
          break;
        }
        case ReadDataType::Displacement: {
          displacements = new double[vertexSize[i] * nDim];
          solverInterface.readBlockVectorData(displDeltaID[indexMarkerWetMappingLocalToGlobal[i]], vertexSize[i],
                                              vertexIDs[i], displacements);
          break;
        }
        case ReadDataType::Temperature: {
          temperatures = new double[vertexSize[i]];
          solverInterface.readBlockScalarData(tempID[indexMarkerWetMappingLocalToGlobal[i]], vertexSize[i],
                                              vertexIDs[i], temperatures);

          break;
        }
        default:
          assert(false);
      }

      if (verbosityLevel_high) {
        cout << "Process #" << solverProcessIndex << "/" << solverProcessSize - 1
             << ": Advancing preCICE: ...done reading " << readDataString << "s for "
             << config_container[ZONE_0]->GetpreCICE_WetSurfaceMarkerName() << indexMarkerWetMappingLocalToGlobal[i]
             << "." << endl;
      }

      // 5. Set displacements/displacementDeltas/Temperatures
      if (verbosityLevel_high) {
        cout << "Process #" << solverProcessIndex << "/" << solverProcessSize - 1 << ": Advancing preCICE: Setting "
             << readDataString << "s for " << config_container[ZONE_0]->GetpreCICE_WetSurfaceMarkerName()
             << indexMarkerWetMappingLocalToGlobal[i] << "..." << endl;
      }
      // convert displacementDeltas into displacementDeltas_su2 -- unnecessary step for temperatures
      switch (readDataType) {
        case ReadDataType::DisplacementDelta: {
          // convert displacementDeltas into displacementDeltas_su2
          for (int iVertex = 0; iVertex < vertexSize[i]; iVertex++) {
            for (int iDim = 0; iDim < nDim; iDim++) {
              displacementDeltas_su2[iVertex][iDim] = displacementDeltas[iVertex * nDim + iDim];
            }
          }
          break;
        }
        case ReadDataType::Displacement: {
          for (int iVertex = 0; iVertex < vertexSize[i]; iVertex++) {
            for (int iDim = 0; iDim < nDim; iDim++) {
              displacementDeltas_su2[iVertex][iDim] =
                  displacements[iVertex * nDim + iDim] - displacements_n[iVertex * nDim + iDim];

              if (solverInterface.isTimeWindowComplete()) {
                displacements_n[iVertex * nDim + iDim] = displacements[iVertex * nDim + iDim];
              }
            }
          }
          break;
        }
        case ReadDataType::Temperature: {
          break;// Do nothing
        }
        default:
          assert(false);
      }

      if (displacementDeltas != NULL) {
        delete[] displacementDeltas;
      }
      if (displacements != NULL) {
        delete[] displacements;
      }
      // temperatures not yet de-allocated as the values in the array itself are used

      // Set change of coordinates (i.e. displacementDeltas) OR set temperatures
      for (int iVertex = 0; iVertex < vertexSize[i]; iVertex++) {
        if (readDataType != ReadDataType::Temperature) {
          geometry_container[ZONE_0][INST_0][MESH_0]->vertex[valueMarkerWet[i]][iVertex]->SetVarCoord(
              displacementDeltas_su2[iVertex]);
        } else {  // Else we are doing CHT
          geometry_container[ZONE_0][INST_0][MESH_0]->SetCustomBoundaryTemperature(valueMarkerWet[i], iVertex,
                                                                                   temperatures[iVertex]);
        }
      }

      // Now de-allocate temperatures
      if (temperatures != NULL) {
        delete[] temperatures;
      }

      if (verbosityLevel_high) {
        cout << "Process #" << solverProcessIndex << "/" << solverProcessSize - 1
             << ": Advancing preCICE: ...done setting " << readDataString << "s for "
             << config_container[ZONE_0]->GetpreCICE_WetSurfaceMarkerName() << indexMarkerWetMappingLocalToGlobal[i]
             << "." << endl;
      }
    }

    if (verbosityLevel_high) {
      cout << "Process #" << solverProcessIndex << "/" << solverProcessSize - 1 << ": ...done advancing preCICE!"
           << endl;
    }
    return max_precice_dt;
  } else {
    if (verbosityLevel_high) {
      cout << "Process #" << solverProcessIndex << "/" << solverProcessSize - 1 << ": Advancing preCICE..." << endl;
    }
    // 3. Advance solverInterface
    if (verbosityLevel_high) {
      cout << "Process #" << solverProcessIndex << "/" << solverProcessSize - 1
           << ": Advancing preCICE: Advancing SolverInterface..." << endl;
    }
    double max_precice_dt;
    max_precice_dt = solverInterface.advance(computedTimestepLength);
    if (verbosityLevel_high) {
      cout << "Process #" << solverProcessIndex << "/" << solverProcessSize - 1
           << ": Advancing preCICE: ...done advancing SolverInterface." << endl;
      cout << "Process #" << solverProcessIndex << "/" << solverProcessSize - 1 << ": ...done advancing preCICE!"
           << endl;
    }
    return max_precice_dt;
  }
}

bool Precice::isCouplingOngoing() { return solverInterface.isCouplingOngoing(); }

bool Precice::isActionRequired(const string& action) { return solverInterface.isActionRequired(action); }

const string& Precice::getCowic() { return cowic; }

const string& Precice::getCoric() { return coric; }

void Precice::saveOldState(bool* StopCalc, double* dt) {
  for (int iPoint = 0; iPoint < nPoint; iPoint++) {
    for (int iVar = 0; iVar < nVar; iVar++) {
      // Save solutions at last and current time step
      solution_Saved[iPoint][iVar] =
          (solver_container[ZONE_0][INST_0][MESH_0][FLOW_SOL]->GetNodes()->GetSolution(iPoint, iVar));
      solution_time_n_Saved[iPoint][iVar] =
          (solver_container[ZONE_0][INST_0][MESH_0][FLOW_SOL]->GetNodes()->GetSolution_time_n(iPoint, iVar));
      solution_time_n1_Saved[iPoint][iVar] =
          (solver_container[ZONE_0][INST_0][MESH_0][FLOW_SOL]->GetNodes()->GetSolution_time_n1(iPoint, iVar));
    }
    if (readDataType != ReadDataType::Temperature) {  // If not doing FSI, no need to save this stuff
      for (int iDim = 0; iDim < nDim; iDim++) {
        // Save coordinates at last, current and next time step
        Coord_Saved[iPoint][iDim] = (geometry_container[ZONE_0][INST_0][MESH_0]->nodes->GetCoord(iPoint))[iDim];
        Coord_n_Saved[iPoint][iDim] = (geometry_container[ZONE_0][INST_0][MESH_0]->nodes->GetCoord_n(iPoint))[iDim];
        Coord_n1_Saved[iPoint][iDim] = (geometry_container[ZONE_0][INST_0][MESH_0]->nodes->GetCoord_n1(iPoint))[iDim];
        Coord_p1_Saved[iPoint][iDim] = (geometry_container[ZONE_0][INST_0][MESH_0]->nodes->GetCoord_p1(iPoint))[iDim];

        // Save grid velocity - only important when using continunous adjoint
        // Also: SU2 does not instantiate GridVel_Grad in CPoint when not, so this check is critical
        if (config_container[ZONE_0]->GetContinuous_Adjoint()) {
          GridVel_Saved[iPoint][iDim] = (geometry_container[ZONE_0][INST_0][MESH_0]->nodes->GetGridVel(iPoint))[iDim];
          for (int jDim = 0; jDim < nDim; jDim++) {
            // Save grid velocity gradient
            GridVel_Grad_Saved[iPoint][iDim][jDim] =
                geometry_container[ZONE_0][INST_0][MESH_0]->nodes->GetGridVel_Grad(iPoint)[iDim][jDim];
          }
        }
      }
    }
  }

  // Save wether simulation should be stopped after the current iteration
  StopCalc_savedState = *StopCalc;
  // Save the time step size
  dt_savedState = *dt;
  // Writing task has been fulfilled successfully
  solverInterface.markActionFulfilled(cowic);
}

void Precice::reloadOldState(bool* StopCalc, double* dt) {
  for (int iPoint = 0; iPoint < nPoint; iPoint++) {
    // Reload solutions at last and current time step
    solver_container[ZONE_0][INST_0][MESH_0][FLOW_SOL]->GetNodes()->SetSolution(iPoint, solution_Saved[iPoint]);
    solver_container[ZONE_0][INST_0][MESH_0][FLOW_SOL]->GetNodes()->Set_Solution_time_n(iPoint,
                                                                                        solution_time_n_Saved[iPoint]);
    solver_container[ZONE_0][INST_0][MESH_0][FLOW_SOL]->GetNodes()->Set_Solution_time_n1(
        iPoint, solution_time_n1_Saved[iPoint]);

    if (readDataType != ReadDataType::Temperature) {  // If not doing FSI, no need to save this stuff
      // Reload coordinates at last, current and next time step
      geometry_container[ZONE_0][INST_0][MESH_0]->nodes->SetCoord_n1(iPoint, Coord_n1_Saved[iPoint]);
      geometry_container[ZONE_0][INST_0][MESH_0]->nodes->SetCoord_n(iPoint, Coord_n_Saved[iPoint]);
      geometry_container[ZONE_0][INST_0][MESH_0]->nodes->SetCoord_p1(iPoint, Coord_p1_Saved[iPoint]);
      geometry_container[ZONE_0][INST_0][MESH_0]->nodes->SetCoord(iPoint, Coord_Saved[iPoint]);

      // Reload grid velocity
      geometry_container[ZONE_0][INST_0][MESH_0]->nodes->SetGridVel(iPoint, GridVel_Saved[iPoint]);

      // Reload grid velocity gradient if using CA
      if (config_container[ZONE_0]->GetContinuous_Adjoint()) {
        for (int iDim = 0; iDim < nDim; iDim++) {
          for (int jDim = 0; jDim < nDim; jDim++) {
            geometry_container[ZONE_0][INST_0][MESH_0]->nodes->GetGridVel_Grad()[iPoint][iDim][jDim] =
                GridVel_Grad_Saved[iPoint][iDim][jDim];
          }
        }
      }
    }
  }

  // Reload wether simulation should be stopped after current iteration
  *StopCalc = StopCalc_savedState;
  // Reload the time step size
  *dt = dt_savedState;
  // Reading task has been fulfilled successfully
  solverInterface.markActionFulfilled(coric);
}

void Precice::finalize() {
  cout << "Process #" << solverProcessIndex << "/" << solverProcessSize - 1 << ": Finalizing preCICE..." << endl;
  solverInterface.finalize();
  cout << "Process #" << solverProcessIndex << "/" << solverProcessSize - 1 << ": Done finalizing preCICE!" << endl;
}
