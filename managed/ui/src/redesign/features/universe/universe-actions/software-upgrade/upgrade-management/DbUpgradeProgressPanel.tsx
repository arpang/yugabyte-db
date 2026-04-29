import { useState } from 'react';
import { yba } from '@yugabyte-ui-library/core';
import { makeStyles, Typography } from '@material-ui/core';
import clsx from 'clsx';
import { Trans, useTranslation } from 'react-i18next';

import { CanaryPauseState, Task } from '@app/redesign/features/tasks/dtos';
import { getPrimaryCluster } from '@app/redesign/utils/universeUtils';
import { formatYbSoftwareVersionString } from '@app/utils/Formatters';
import { Universe } from '@app/v2/api/yugabyteDBAnywhereV2APIs.schemas';
import { getPlacementAzMetadataList } from '../utils/formUtils';
import { AccordionCard, AccordionCardState } from './AccordionCard';
import { AssessPerformancePrompt } from './AssessPerformancePrompt';
import { UpgradeStageCategory } from './constants';
import { PreCheckStageBanner } from './PreCheckStageBanner';
import { UpgradeStageBanner } from './UpgradeStageBanner';
import { classifyDbUpgradeStages } from './utils';
import { TemporaryRestrictionsNotice } from './TemporaryRestrictionsNotice';
import { DbUpgradeRollBackModal } from '../DbUpgradeRollBackModal';

import ConnectIcon from '@app/redesign/assets/approved/connect.svg';

const YBButton = yba.YBButton;
interface DbUpgradeProgressPanelProps {
  dbUpgradeTask: Task;
  universe: Universe | undefined;
  isYsqlMajorUpgrade: boolean;
  className?: string;
}

const useStyles = makeStyles((theme) => ({
  progressPanel: {
    display: 'flex',
    flexDirection: 'column',
    gap: theme.spacing(2),

    padding: theme.spacing(2),

    backgroundColor: theme.palette.grey[50],
    border: `1px solid ${theme.palette.grey[200]}`,
    borderRadius: theme.shape.borderRadius,

    '& .MuiTypography-body2': {
      lineHeight: '20px'
    },
    '& .MuiTypography-subtitle1': {
      lineHeight: '18px'
    }
  },
  title: {
    color: theme.palette.grey[800],
    fontSize: 15,
    fontWeight: 600,
    fontStyle: 'normal',
    lineHeight: '20px'
  },
  infoText: {
    color: theme.palette.grey[700]
  },
  cardButtonsContainer: {
    display: 'flex',
    justifyContent: 'flex-end',
    gap: theme.spacing(1)
  }
}));

const TSERVER_AZ_UPGRADE_STAGE_START_INDEX = 3;
export const DbUpgradeProgressPanel = ({
  dbUpgradeTask,
  universe,
  className,
  isYsqlMajorUpgrade
}: DbUpgradeProgressPanelProps) => {
  const [isDbUpgradeRollbackModalOpen, setIsDbUpgradeRollbackModalOpen] = useState(false);
  const classes = useStyles();
  const { t } = useTranslation('translation', {
    keyPrefix: 'universeActions.dbUpgrade.dbUpgradeManagementSidePanel.progressPanel'
  });

  const handleRollbackUpgrade = () => {
    setIsDbUpgradeRollbackModalOpen(true);
  };
  const handleResumeUpgrade = () => {
    // TODO: PLAT-20428 - Implement resume upgrade logic
  };

  const upgradedAzMetadataList =
    getPlacementAzMetadataList(getPrimaryCluster(universe?.spec?.clusters ?? [])) ?? [];
  const upgradedAzDisplayNameByUuid = Object.fromEntries(
    upgradedAzMetadataList.map((az) => [az.azUuid, az.displayName])
  );
  const upgradeAzStageCount =
    dbUpgradeTask.softwareUpgradeProgress?.tserverAZUpgradeStatesList?.length ?? 0;

  const { preCheckStage, upgradeMasterServersStage, upgradeAzStages, finalizeStage } =
    classifyDbUpgradeStages(dbUpgradeTask);
  const isPausedAfterTservers =
    dbUpgradeTask.softwareUpgradeProgress?.canaryPauseState ===
    CanaryPauseState.PAUSED_AFTER_TSERVERS_AZ;
  const isPausedAfterMasters =
    dbUpgradeTask.softwareUpgradeProgress?.canaryPauseState ===
    CanaryPauseState.PAUSED_AFTER_MASTERS;
  const isPausedAfterSuccessfulMasterServersUpgrade =
    isPausedAfterMasters && upgradeMasterServersStage === AccordionCardState.SUCCESS;
  const isTserverAzUpgradeStagesCompleted = Object.values(upgradeAzStages).every(
    (azUpgradeStage) => azUpgradeStage.accordionCardState === AccordionCardState.SUCCESS
  );
  const targetDbVersionLabel = dbUpgradeTask.details?.versionNumbers?.ybSoftwareVersion
    ? formatYbSoftwareVersionString(dbUpgradeTask.details?.versionNumbers?.ybSoftwareVersion)
    : '-';
  const taskUuid = dbUpgradeTask.id ?? '';

  return (
    <div className={clsx(classes.progressPanel, className)}>
      <Typography variant="h5" className={classes.title}>
        {t('title')}
      </Typography>
      <AccordionCard title={t('preCheckStage.title')} stepNumber={1} state={preCheckStage}>
        <Typography variant="subtitle1" className={classes.infoText}>
          {t('preCheckStage.description')}
        </Typography>
        <PreCheckStageBanner
          state={preCheckStage}
          universeUuid={universe?.info?.universe_uuid ?? ''}
          taskUuid={taskUuid}
        />
      </AccordionCard>
      <AccordionCard
        title={t('upgradeMasterServersStage.title')}
        stepNumber={2}
        state={upgradeMasterServersStage}
      >
        <Typography variant="subtitle1" className={classes.infoText}>
          <Trans
            t={t}
            i18nKey="upgradeMasterServersStage.description"
            components={{ bold: <b /> }}
            values={{ version: targetDbVersionLabel }}
          />
        </Typography>
        {upgradeMasterServersStage === AccordionCardState.NEUTRAL && (
          <TemporaryRestrictionsNotice
            upgradeStageCategory={UpgradeStageCategory.UPGRADE}
            isYsqlMajorUpgrade={isYsqlMajorUpgrade}
          />
        )}
        {isPausedAfterSuccessfulMasterServersUpgrade && (
          <AssessPerformancePrompt upgradeStageCategory={UpgradeStageCategory.UPGRADE} />
        )}
        <UpgradeStageBanner
          state={upgradeMasterServersStage}
          universeUuid={universe?.info?.universe_uuid ?? ''}
          taskUuid={taskUuid}
        />
        {isPausedAfterSuccessfulMasterServersUpgrade && (
          <div className={classes.cardButtonsContainer}>
            <YBButton
              variant="secondary"
              onClick={handleRollbackUpgrade}
              dataTestId="roll-back-upgrade-button-masters"
            >
              {t('rollBack')}
            </YBButton>
            <YBButton
              variant="ybaPrimary"
              onClick={handleResumeUpgrade}
              dataTestId="resume-upgrade-button-masters"
            >
              {t('resumeUpgrade')}
            </YBButton>
          </div>
        )}
      </AccordionCard>
      {dbUpgradeTask.softwareUpgradeProgress?.tserverAZUpgradeStatesList.map(
        (azUpgradeState, index) => {
          const azUpgradeStagePresentation = upgradeAzStages[azUpgradeState.azUUID];
          const cardState =
            azUpgradeStagePresentation?.accordionCardState ?? AccordionCardState.NEUTRAL;
          const isPausedAfterSuccessfulUpgrade =
            cardState === AccordionCardState.SUCCESS &&
            isPausedAfterTservers &&
            azUpgradeStagePresentation?.isLastAzBeforeCanaryPause;
          return (
            <AccordionCard
              key={azUpgradeState.azUUID}
              title={t('upgradeAzStage.title', {
                azLabel: upgradedAzDisplayNameByUuid[azUpgradeState.azUUID] ?? azUpgradeState.azName
              })}
              stepNumber={TSERVER_AZ_UPGRADE_STAGE_START_INDEX + index}
              state={cardState}
            >
              <Typography variant="subtitle1" className={classes.infoText}>
                <Trans
                  t={t}
                  i18nKey="upgradeAzStage.description"
                  components={{ bold: <b /> }}
                  values={{ azName: azUpgradeState.azName, version: targetDbVersionLabel }}
                />
              </Typography>
              {cardState === AccordionCardState.NEUTRAL && (
                <TemporaryRestrictionsNotice
                  upgradeStageCategory={UpgradeStageCategory.UPGRADE}
                  isYsqlMajorUpgrade={isYsqlMajorUpgrade}
                />
              )}
              {isPausedAfterSuccessfulUpgrade && (
                <AssessPerformancePrompt upgradeStageCategory={UpgradeStageCategory.UPGRADE} />
              )}
              <UpgradeStageBanner
                state={cardState}
                universeUuid={universe?.info?.universe_uuid ?? ''}
                taskUuid={taskUuid}
              />
              {isPausedAfterSuccessfulUpgrade && (
                <div className={classes.cardButtonsContainer}>
                  <YBButton
                    variant="secondary"
                    onClick={handleRollbackUpgrade}
                    dataTestId={`roll-back-upgrade-button-tserverAz-${azUpgradeState.azUUID}`}
                  >
                    {t('rollBack')}
                  </YBButton>
                  <YBButton
                    variant="ybaPrimary"
                    onClick={handleResumeUpgrade}
                    dataTestId={`resume-upgrade-button-tserverAz-${azUpgradeState.azUUID}`}
                  >
                    {t('resumeUpgrade')}
                  </YBButton>
                </div>
              )}
            </AccordionCard>
          );
        }
      )}
      <AccordionCard
        title={t('finalizeStage.title')}
        stepNumber={TSERVER_AZ_UPGRADE_STAGE_START_INDEX + upgradeAzStageCount}
        state={finalizeStage}
      >
        <AssessPerformancePrompt upgradeStageCategory={UpgradeStageCategory.FINALIZE} />
        {finalizeStage === AccordionCardState.NEUTRAL && (
          <TemporaryRestrictionsNotice
            upgradeStageCategory={UpgradeStageCategory.FINALIZE}
            isYsqlMajorUpgrade={isYsqlMajorUpgrade}
          />
        )}
        {isTserverAzUpgradeStagesCompleted && (
          <div className={classes.cardButtonsContainer}>
            <YBButton
              variant="secondary"
              onClick={handleRollbackUpgrade}
              dataTestId="roll-back-upgrade-button-finalize"
            >
              {t('rollBack')}
            </YBButton>
            <YBButton
              variant="ybaPrimary"
              startIcon={<ConnectIcon width={24} height={24} />}
              onClick={handleResumeUpgrade}
              dataTestId="finalize-upgrade-now-button"
            >
              {t('finalizeUpgradeNow')}
            </YBButton>
          </div>
        )}
      </AccordionCard>
      <DbUpgradeRollBackModal
        modalProps={{
          open: isDbUpgradeRollbackModalOpen,
          onClose: () => setIsDbUpgradeRollbackModalOpen(false)
        }}
        universeUuid={universe?.info?.universe_uuid ?? ''}
      />
    </div>
  );
};
